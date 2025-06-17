// Source/session_handle_query_execution.cpp
#include <iostream>
#include <utility>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"  // <-- 添加这个头文件
#include "neo4j_bolt_transport/result_stream.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    std::pair<std::pair<boltprotocol::BoltError, std::string>, std::unique_ptr<BoltResultStream>> SessionHandle::run_query(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override) {
        boltprotocol::SuccessMessageParams run_summary;
        boltprotocol::FailureMessageParams run_failure_details;
        std::pair<boltprotocol::BoltError, std::string> prepare_result = {boltprotocol::BoltError::SUCCESS, ""};
        std::optional<int64_t> qid_for_stream;
        bool server_can_have_more_records_after_run = false;
        std::shared_ptr<spdlog::logger> logger = nullptr;

        std::pair<boltprotocol::BoltError, std::string> conn_check_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_pair, "run_query (initial check)");

        if (conn) {
            logger = conn->get_logger();
        } else if (transport_manager_) {  // 确保 transport_manager_ 不是 nullptr
            // 现在 Neo4jBoltTransport 的定义是完整的
            logger = transport_manager_->get_config().logger;
        }

        if (!conn) {
            prepare_result = conn_check_pair;
            if (logger) logger->warn("[SessionExec] run_query: Connection unavailable. Error: {}, Msg: {}", static_cast<int>(prepare_result.first), prepare_result.second);
        } else {
            if (is_in_transaction()) {
                prepare_result = _prepare_explicit_tx_stream(cypher, parameters, run_summary, run_failure_details);
                if (prepare_result.first == boltprotocol::BoltError::SUCCESS) {
                    qid_for_stream = current_transaction_query_id_;
                    server_can_have_more_records_after_run = true;
                }
            } else {
                prepare_result = _prepare_auto_commit_stream(cypher, parameters, tx_metadata_override, run_summary, run_failure_details);
                if (prepare_result.first == boltprotocol::BoltError::SUCCESS) {
                    qid_for_stream = std::nullopt;
                    server_can_have_more_records_after_run = true;
                }
            }
        }

        std::shared_ptr<std::vector<std::string>> fields_ptr = std::make_shared<std::vector<std::string>>();
        auto it_fields = run_summary.metadata.find("fields");
        if (it_fields != run_summary.metadata.end() && std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second)) {
            const auto& list_ptr = std::get<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second);
            if (list_ptr) {
                fields_ptr->reserve(list_ptr->elements.size());
                for (const auto& field_val : list_ptr->elements) {
                    if (std::holds_alternative<std::string>(field_val)) {
                        fields_ptr->push_back(std::get<std::string>(field_val));
                    }
                }
            }
        }
        if (logger && prepare_result.first == boltprotocol::BoltError::SUCCESS) {
            logger->debug("[SessionExec] run_query successful prep. Fields: {}. QID for stream: {}", fields_ptr->size(), qid_for_stream.has_value() ? std::to_string(qid_for_stream.value()) : "none");
        }

        auto result_stream = std::make_unique<BoltResultStream>(this,
                                                                qid_for_stream,
                                                                std::move(run_summary),
                                                                fields_ptr,
                                                                std::vector<boltprotocol::RecordMessageParams>{},
                                                                server_can_have_more_records_after_run,
                                                                prepare_result.first,
                                                                prepare_result.second,
                                                                (prepare_result.first != boltprotocol::BoltError::SUCCESS ? std::make_optional(run_failure_details) : std::nullopt));

        return {std::move(prepare_result), std::move(result_stream)};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::run_consume(
        const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, boltprotocol::SuccessMessageParams& out_final_summary, boltprotocol::FailureMessageParams& out_failure_details, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (connection_ && connection_->get_logger()) {
            logger = connection_->get_logger();
        } else if (transport_manager_) {  // 确保 transport_manager_ 不是 nullptr
                                          // 现在 Neo4jBoltTransport 的定义是完整的
            logger = transport_manager_->get_config().logger;
        }

        if (logger) logger->trace("[SessionExec] run_consume starting for cypher: {:.30}...", cypher);

        auto [initial_err_pair, result_stream_ptr] = run_query(cypher, parameters, tx_metadata_override);

        if (initial_err_pair.first != boltprotocol::BoltError::SUCCESS) {
            if (logger) logger->warn("[SessionExec] run_consume: run_query failed initially. Error: {}, Msg: {}", static_cast<int>(initial_err_pair.first), initial_err_pair.second);
            if (result_stream_ptr) {
                out_failure_details = result_stream_ptr->get_failure_details();
            } else {
                out_failure_details.metadata["message"] = boltprotocol::Value(initial_err_pair.second);
            }
            return initial_err_pair;
        }

        if (!result_stream_ptr) {
            if (logger) logger->error("[SessionExec] run_consume: Internal error - run_query succeeded but returned null stream.");
            out_failure_details.metadata["message"] = boltprotocol::Value("Internal error: run_query succeeded but returned null stream.");
            return {boltprotocol::BoltError::UNKNOWN_ERROR, "Null result stream post run_query."};
        }

        if (result_stream_ptr->has_failed()) {
            if (logger) logger->warn("[SessionExec] run_consume: Stream was already failed after creation. Reason: {}, Msg: {}", static_cast<int>(result_stream_ptr->get_failure_reason()), result_stream_ptr->get_failure_message());
            out_failure_details = result_stream_ptr->get_failure_details();
            return {result_stream_ptr->get_failure_reason(), result_stream_ptr->get_failure_message()};
        }

        auto consume_outcome_tuple = result_stream_ptr->consume();
        boltprotocol::BoltError consume_err_code = std::get<0>(consume_outcome_tuple);
        std::string consume_err_msg = std::get<1>(std::move(consume_outcome_tuple));
        out_final_summary = std::get<2>(std::move(consume_outcome_tuple));

        if (consume_err_code != boltprotocol::BoltError::SUCCESS) {
            if (logger) logger->warn("[SessionExec] run_consume: stream consume failed. Error: {}, Msg: {}", static_cast<int>(consume_err_code), consume_err_msg);
            out_failure_details = result_stream_ptr->get_failure_details();
            return {consume_err_code, std::move(consume_err_msg)};
        }

        if (logger) logger->trace("[SessionExec] run_consume successful.");

        if (!connection_is_valid_) {
            if (connection_ && connection_->get_last_error() != boltprotocol::BoltError::SUCCESS) {
                if (logger) logger->warn("[SessionExec] run_consume: Connection became invalid. Last conn error: {}", connection_->get_last_error_message());
                return {connection_->get_last_error(), connection_->get_last_error_message()};
            }
            if (logger) logger->warn("[SessionExec] run_consume: Connection became invalid (no specific error).");
            return {boltprotocol::BoltError::NETWORK_ERROR, "Connection lost during operation."};
        }

        return {boltprotocol::BoltError::SUCCESS, ""};
    }

}  // namespace neo4j_bolt_transport