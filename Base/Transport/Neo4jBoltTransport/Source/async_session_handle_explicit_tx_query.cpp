#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> AsyncSessionHandle::run_query_in_transaction_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }
        ResultSummary default_summary_on_error({},
                                               stream_context_ ? stream_context_->negotiated_bolt_version : boltprotocol::versions::Version(0, 0),
                                               stream_context_ ? stream_context_->utc_patch_active : false,
                                               stream_context_ ? (stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port)) : "unknown_async_tx_run",
                                               session_params_.database_name);

        if (!is_valid() || !stream_context_) {
            if (logger) logger->warn("[AsyncSessionTXQuery] run_query_in_transaction_async on invalid session.");
            co_return std::make_pair(boltprotocol::BoltError::NETWORK_ERROR, std::move(default_summary_on_error));
        }
        if (!in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncSessionTXQuery] run_query_in_transaction_async: Not in an explicit transaction.");
            co_return std::make_pair(boltprotocol::BoltError::INVALID_ARGUMENT, std::move(default_summary_on_error));
        }

        if (logger) logger->debug("[AsyncSessionTXQuery] run_query_in_transaction_async: Cypher: {:.50}...", cypher);

        boltprotocol::RunMessageParams explicit_tx_run_params;
        explicit_tx_run_params.cypher_query = cypher;
        explicit_tx_run_params.parameters = parameters;

        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_writer(run_payload_bytes);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_run_message(explicit_tx_run_params, run_writer, stream_context_->negotiated_bolt_version);
        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize RUN (in TX): " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionTXQuery] {}", last_error_message_);
            co_return std::make_pair(last_error_code_, std::move(default_summary_on_error));
        }

        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionTXQuery:StaticOpErrHandler] RUN (in TX) Error: {} - {}", static_cast<int>(reason), message);
        };

        auto [run_summary_err, run_result_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, run_payload_bytes, stream_context_->original_config, logger, static_op_error_handler);

        if (run_summary_err != boltprotocol::BoltError::SUCCESS) {
            co_return std::make_pair(last_error_code_, std::move(run_result_summary_obj));
        }

        last_tx_run_qid_.reset();
        if (stream_context_->negotiated_bolt_version.major >= 4) {
            auto it_qid = run_result_summary_obj.raw_params().metadata.find("qid");
            if (it_qid != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
                last_tx_run_qid_ = std::get<int64_t>(it_qid->second);
                if (logger) logger->trace("[AsyncSessionTXQuery] RUN (in TX) got qid: {}", *last_tx_run_qid_);
            } else if (logger) {
                logger->trace("[AsyncSessionTXQuery] RUN (in TX) SUCCESS did not contain 'qid'.");
            }
        }

        ResultSummary final_summary_for_tx_run = run_result_summary_obj;
        bool server_has_more_pull = true;
        auto it_run_has_more = run_result_summary_obj.raw_params().metadata.find("has_more");
        if (it_run_has_more != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<bool>(it_run_has_more->second)) {
            server_has_more_pull = std::get<bool>(it_run_has_more->second);
        } else {
            auto it_fields = run_result_summary_obj.raw_params().metadata.find("fields");
            if (it_fields == run_result_summary_obj.raw_params().metadata.end() ||
                (std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second) && std::get<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second) && std::get<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second)->elements.empty())) {
                server_has_more_pull = false;
            }
        }
        if (!server_has_more_pull && logger) {
            logger->trace("[AsyncSessionTXQuery] RUN (in TX) indicates no records to PULL.");
        }

        if (server_has_more_pull) {
            boltprotocol::PullMessageParams pull_params;
            pull_params.n = -1;
            if (last_tx_run_qid_.has_value() && stream_context_->negotiated_bolt_version.major >= 4) {
                pull_params.qid = last_tx_run_qid_;
            }
            std::vector<uint8_t> pull_payload_bytes;
            boltprotocol::PackStreamWriter pull_writer(pull_payload_bytes);
            serialize_err = boltprotocol::serialize_pull_message(pull_params, pull_writer);
            if (serialize_err != boltprotocol::BoltError::SUCCESS) {
                last_error_code_ = serialize_err;
                last_error_message_ = "Failed to serialize PULL (in TX): " + error::bolt_error_to_string(serialize_err);
                if (logger) logger->error("[AsyncSessionTXQuery] {}", last_error_message_);
                co_return std::make_pair(last_error_code_, std::move(final_summary_for_tx_run));
            }

            boltprotocol::BoltError send_pull_err = co_await internal::BoltPhysicalConnection::_send_chunked_payload_async_static_helper(*stream_context_, std::move(pull_payload_bytes), stream_context_->original_config, logger, static_op_error_handler);

            if (send_pull_err != boltprotocol::BoltError::SUCCESS) {
                co_return std::make_pair(last_error_code_, std::move(final_summary_for_tx_run));
            }

            bool current_pull_summary_received = false;
            while (!current_pull_summary_received) {
                auto [recv_err, response_payload] = co_await internal::BoltPhysicalConnection::_receive_chunked_payload_async_static_helper(*stream_context_, stream_context_->original_config, logger, static_op_error_handler);

                if (recv_err != boltprotocol::BoltError::SUCCESS) {
                    co_return std::make_pair(last_error_code_, std::move(final_summary_for_tx_run));
                }
                if (response_payload.empty()) {
                    if (logger) logger->trace("[AsyncSessionTXQuery] PULL (in TX) loop received NOOP.");
                    continue;
                }

                boltprotocol::MessageTag tag;
                boltprotocol::PackStreamReader peek_reader_temp(response_payload);
                uint8_t raw_tag_byte_peek = 0;
                uint32_t num_fields_peek = 0;
                boltprotocol::BoltError peek_err = boltprotocol::peek_message_structure_header(peek_reader_temp, raw_tag_byte_peek, num_fields_peek);
                if (peek_err != boltprotocol::BoltError::SUCCESS) {
                    static_op_error_handler(peek_err, "Failed to peek tag in PULL (in TX) response");
                    co_return std::make_pair(last_error_code_, std::move(final_summary_for_tx_run));
                }
                tag = static_cast<boltprotocol::MessageTag>(raw_tag_byte_peek);

                boltprotocol::PackStreamReader reader(response_payload);
                if (tag == boltprotocol::MessageTag::RECORD) {
                    if (logger) logger->trace("[AsyncSessionTXQuery] Consumed a RECORD message (in TX).");
                } else if (tag == boltprotocol::MessageTag::SUCCESS) {
                    boltprotocol::SuccessMessageParams pull_summary_meta;
                    boltprotocol::BoltError deser_err = boltprotocol::deserialize_success_message(reader, pull_summary_meta);
                    if (deser_err != boltprotocol::BoltError::SUCCESS) {
                        static_op_error_handler(deser_err, "Failed to deserialize SUCCESS from PULL (in TX)");
                        co_return std::make_pair(last_error_code_, std::move(final_summary_for_tx_run));
                    }
                    final_summary_for_tx_run =
                        ResultSummary(std::move(pull_summary_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name);

                    auto it_has_more = final_summary_for_tx_run.raw_params().metadata.find("has_more");
                    if (it_has_more != final_summary_for_tx_run.raw_params().metadata.end() && std::holds_alternative<bool>(it_has_more->second)) {
                        server_has_more_pull = std::get<bool>(it_has_more->second);
                    } else {
                        server_has_more_pull = false;
                    }
                    if (!server_has_more_pull) current_pull_summary_received = true;
                    if (logger) logger->trace("[AsyncSessionTXQuery] PULL (in TX) SUCCESS received. HasMore: {}", server_has_more_pull);

                } else if (tag == boltprotocol::MessageTag::FAILURE) {
                    boltprotocol::FailureMessageParams pull_failure_meta;
                    boltprotocol::BoltError deser_err = boltprotocol::deserialize_failure_message(reader, pull_failure_meta);
                    if (deser_err != boltprotocol::BoltError::SUCCESS) {
                        static_op_error_handler(deser_err, "Failed to deserialize FAILURE from PULL (in TX)");
                    } else {
                        std::string fail_detail = error::format_server_failure(pull_failure_meta);
                        static_op_error_handler(boltprotocol::BoltError::UNKNOWN_ERROR, "Server FAILURE during PULL (in TX): " + fail_detail);
                    }
                    final_summary_for_tx_run = ResultSummary(boltprotocol::SuccessMessageParams{std::move(pull_failure_meta.metadata)},
                                                             stream_context_->negotiated_bolt_version,
                                                             stream_context_->utc_patch_active,
                                                             stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port),
                                                             session_params_.database_name);
                    co_return std::make_pair(last_error_code_, std::move(final_summary_for_tx_run));
                } else {
                    static_op_error_handler(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Unexpected tag " + std::to_string(static_cast<int>(tag)) + " during PULL (in TX)");
                    co_return std::make_pair(last_error_code_, std::move(final_summary_for_tx_run));
                }
            }
        }

        last_error_code_ = boltprotocol::BoltError::SUCCESS;
        last_error_message_ = "";
        if (logger) logger->info("[AsyncSessionTXQuery] run_query_in_transaction_async successful for: {:.50}...", cypher);
        co_return std::make_pair(boltprotocol::BoltError::SUCCESS, std::move(final_summary_for_tx_run));
    }

}  // namespace neo4j_bolt_transport