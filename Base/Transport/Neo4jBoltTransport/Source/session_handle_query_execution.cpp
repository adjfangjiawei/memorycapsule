#include <iostream>
#include <utility>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"
#include "neo4j_bolt_transport/result_stream.h"  // 包含 ResultSummary
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    std::pair<std::pair<boltprotocol::BoltError, std::string>, std::unique_ptr<BoltResultStream>> SessionHandle::run_query(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override) {
        boltprotocol::SuccessMessageParams run_summary_raw;
        boltprotocol::FailureMessageParams run_failure_details_raw;
        std::pair<boltprotocol::BoltError, std::string> prepare_result = {boltprotocol::BoltError::SUCCESS, ""};
        std::optional<int64_t> qid_for_stream;
        bool server_can_have_more_records_after_run = false;  // Default to false, set true if RUN succeeds

        std::shared_ptr<spdlog::logger> logger = nullptr;
        std::string current_server_address = "unknown_server:0";
        boltprotocol::versions::Version current_bolt_version(0, 0);
        bool current_utc_patch_active = false;

        std::pair<boltprotocol::BoltError, std::string> conn_check_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_pair, "run_query (initial check)");

        if (conn) {
            logger = conn->get_logger();
            current_server_address = conn->get_config().target_host + ":" + std::to_string(conn->get_config().target_port);
            current_bolt_version = conn->get_bolt_version();
            current_utc_patch_active = conn->is_utc_patch_active();
        } else if (transport_manager_ && transport_manager_->get_config().logger) {  // 安全访问
            logger = transport_manager_->get_config().logger;
        }

        if (!conn) {
            prepare_result = conn_check_pair;
            if (logger) logger->warn("[SessionExec] run_query: Connection unavailable. Error: {}, Msg: {}", static_cast<int>(prepare_result.first), prepare_result.second);
        } else {
            if (is_in_transaction()) {
                // tx_metadata_override and tx_timeout are not applicable for RUN within an explicit transaction
                // Those are set during BEGIN.
                if (tx_metadata_override.has_value() && logger) {
                    logger->trace("[SessionExec] tx_metadata_override provided for RUN in explicit transaction, will be ignored.");
                }
                prepare_result = _prepare_explicit_tx_run(cypher, parameters, run_summary_raw, run_failure_details_raw);
                if (prepare_result.first == boltprotocol::BoltError::SUCCESS) {
                    qid_for_stream = current_transaction_query_id_;
                    server_can_have_more_records_after_run = true;  // Assume RUN in TX might yield records
                }
            } else {  // Auto-commit
                prepare_result = _prepare_auto_commit_run(cypher, parameters, tx_metadata_override, run_summary_raw, run_failure_details_raw);
                if (prepare_result.first == boltprotocol::BoltError::SUCCESS) {
                    qid_for_stream = std::nullopt;  // qid might be in run_summary_raw for auto-commit
                    auto it_qid = run_summary_raw.metadata.find("qid");
                    if (it_qid != run_summary_raw.metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
                        qid_for_stream = std::get<int64_t>(it_qid->second);
                    }
                    server_can_have_more_records_after_run = true;
                }
            }
        }

        // Extract field names even if prepare_result failed, as run_summary_raw might have partial data
        std::shared_ptr<std::vector<std::string>> fields_ptr = std::make_shared<std::vector<std::string>>();
        auto it_fields = run_summary_raw.metadata.find("fields");
        if (it_fields != run_summary_raw.metadata.end() && std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second)) {
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

        // Create BoltResultStream, passing all necessary context.
        // If prepare_result indicates failure, the stream will be initialized in a failed state.
        auto result_stream = std::make_unique<BoltResultStream>(this,
                                                                qid_for_stream,
                                                                std::move(run_summary_raw),  // Moves the raw summary from RUN
                                                                fields_ptr,
                                                                std::vector<boltprotocol::RecordMessageParams>{},  // RUN does not pipeline records
                                                                server_can_have_more_records_after_run,
                                                                current_bolt_version,
                                                                current_utc_patch_active,
                                                                current_server_address,
                                                                session_params_.database_name,
                                                                prepare_result.first,                                                                                                    // initial_error
                                                                prepare_result.second,                                                                                                   // initial_error_message
                                                                (prepare_result.first != boltprotocol::BoltError::SUCCESS ? std::make_optional(run_failure_details_raw) : std::nullopt)  // initial_failure_details
        );

        return {std::move(prepare_result), std::move(result_stream)};
    }

    std::pair<std::pair<boltprotocol::BoltError, std::string>, ResultSummary> SessionHandle::run_query_and_consume(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        std::string srv_addr_cache = "unknown_server:0";
        boltprotocol::versions::Version bolt_ver_cache(0, 0);
        bool utc_patch_cache = false;

        // Safely get connection info, even if connection might be invalid
        if (connection_) {  // First check if connection_ is not nullptr
            if (connection_->get_logger()) logger = connection_->get_logger();
            srv_addr_cache = connection_->get_config().target_host + ":" + std::to_string(connection_->get_config().target_port);
            bolt_ver_cache = connection_->get_bolt_version();
            utc_patch_cache = connection_->is_utc_patch_active();
        } else if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (logger) logger->trace("[SessionExec] run_query_and_consume starting for cypher: {:.30}...", cypher);

        auto [initial_err_pair, result_stream_ptr] = run_query(cypher, parameters, tx_metadata_override);

        if (initial_err_pair.first != boltprotocol::BoltError::SUCCESS) {
            if (logger) logger->warn("[SessionExec] run_query_and_consume: run_query failed initially. Error: {}, Msg: {}", static_cast<int>(initial_err_pair.first), initial_err_pair.second);
            // Even if run_query fails, result_stream_ptr might be created (and contain error info)
            // We need its run_summary_typed_'s raw params to construct a fallback ResultSummary
            if (result_stream_ptr) {
                // Create a copy of the raw params from the (failed) stream's run_summary
                boltprotocol::SuccessMessageParams params_copy = result_stream_ptr->get_run_summary().raw_params();
                return {initial_err_pair, ResultSummary(std::move(params_copy), bolt_ver_cache, utc_patch_cache, srv_addr_cache, session_params_.database_name)};
            }
            // If result_stream_ptr is also null, create an empty ResultSummary
            return {initial_err_pair, ResultSummary({}, bolt_ver_cache, utc_patch_cache, srv_addr_cache, session_params_.database_name)};
        }

        if (!result_stream_ptr) {  // Should not happen if initial_err_pair.first is SUCCESS
            if (logger) logger->error("[SessionExec] run_query_and_consume: Internal error - run_query succeeded but returned null stream.");
            return {{boltprotocol::BoltError::UNKNOWN_ERROR, "Null result stream post run_query."}, ResultSummary({}, bolt_ver_cache, utc_patch_cache, srv_addr_cache, session_params_.database_name)};
        }

        // Stream was created successfully, now consume it
        auto [consume_err_code, consume_err_msg, final_summary_typed] = result_stream_ptr->consume();

        if (consume_err_code != boltprotocol::BoltError::SUCCESS) {
            if (logger) logger->warn("[SessionExec] run_query_and_consume: stream consume failed. Error: {}, Msg: {}", static_cast<int>(consume_err_code), consume_err_msg);
            // Return the final_summary_typed from consume(), which might be based on RUN or a partial PULL/DISCARD
            return {{consume_err_code, std::move(consume_err_msg)}, std::move(final_summary_typed)};
        }

        if (logger) logger->trace("[SessionExec] run_query_and_consume successful.");

        // After successful consumption, check if the connection itself became invalid during the process.
        if (!connection_is_valid_) {
            boltprotocol::BoltError conn_last_err = boltprotocol::BoltError::NETWORK_ERROR;
            std::string conn_last_msg = "Connection lost during operation.";
            if (connection_ && connection_->get_last_error_code() != boltprotocol::BoltError::SUCCESS) {
                conn_last_err = connection_->get_last_error_code();
                conn_last_msg = connection_->get_last_error_message();
                if (logger) logger->warn("[SessionExec] run_query_and_consume: Connection became invalid. Last conn error: {}", conn_last_msg);
            } else if (logger) {
                logger->warn("[SessionExec] run_query_and_consume: Connection became invalid (no specific error).");
            }
            return {{conn_last_err, conn_last_msg}, std::move(final_summary_typed)};
        }
        return {{boltprotocol::BoltError::SUCCESS, ""}, std::move(final_summary_typed)};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::run_query_without_result(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override) {
        auto [err_pair_outer, summary_typed] = run_query_and_consume(cypher, parameters, tx_metadata_override);
        // summary_typed is ignored here, only error status is returned
        return err_pair_outer;
    }
}  // namespace neo4j_bolt_transport