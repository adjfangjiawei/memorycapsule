#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/async_result_stream.h"  // Include new AsyncResultStream
#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/bolt_record.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // _prepare_run_message_params implementation remains the same as in Batch 7
    boltprotocol::RunMessageParams AsyncSessionHandle::_prepare_run_message_params(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters) {
        boltprotocol::RunMessageParams run_p;
        run_p.cypher_query = cypher;
        run_p.parameters = parameters;

        if (stream_context_) {
            if (session_params_.database_name.has_value()) {
                run_p.db = session_params_.database_name;
            }
            if (session_params_.impersonated_user.has_value()) {
                run_p.imp_user = session_params_.impersonated_user;
            }

            if (stream_context_->negotiated_bolt_version >= boltprotocol::versions::V5_0) {
                if (session_params_.default_access_mode == config::AccessMode::READ) {
                    run_p.mode = "r";
                }
            } else {
                if (session_params_.default_access_mode == config::AccessMode::READ) {
                    run_p.mode = "r";
                }
            }
            if (transport_manager_ && transport_manager_->get_config().explicit_transaction_timeout_default_ms > 0) {
                run_p.tx_timeout = static_cast<int64_t>(transport_manager_->get_config().explicit_transaction_timeout_default_ms);
            }
        }
        return run_p;
    }

    // run_query_async implementation remains the same as in Batch 7
    boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> AsyncSessionHandle::run_query_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }
        ResultSummary default_summary({},
                                      stream_context_ ? stream_context_->negotiated_bolt_version : boltprotocol::versions::Version(0, 0),
                                      stream_context_ ? stream_context_->utc_patch_active : false,
                                      stream_context_ ? (stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port)) : "unknown_async_run",
                                      session_params_.database_name);

        if (!is_valid() || !stream_context_) {
            std::string err_msg = "AsyncSessionHandle::run_query_async called on invalid or closed session.";
            if (logger) logger->warn("[AsyncSessionExec] {}", err_msg);
            co_return std::make_pair(boltprotocol::BoltError::NETWORK_ERROR, std::move(default_summary));
        }
        if (logger) logger->debug("[AsyncSessionExec] run_query_async: Cypher: {:.50}...", cypher);
        boltprotocol::RunMessageParams run_params = _prepare_run_message_params(cypher, parameters);
        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_writer(run_payload_bytes);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_run_message(run_params, run_writer, stream_context_->negotiated_bolt_version);
        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize RUN message: " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionExec] {}", last_error_message_);
            co_return std::make_pair(last_error_code_, std::move(default_summary));
        }
        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionExec:StaticOpErrHandler] Error: {} - {}", static_cast<int>(reason), message);
        };
        auto [run_summary_err, run_result_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, run_payload_bytes, stream_context_->original_config, logger, static_op_error_handler);
        if (run_summary_err != boltprotocol::BoltError::SUCCESS) {
            co_return std::make_pair(last_error_code_, std::move(run_result_summary_obj));
        }
        boltprotocol::SuccessMessageParams final_pull_success_meta = run_result_summary_obj.raw_params();
        bool server_has_more = true;
        auto it_run_has_more = run_result_summary_obj.raw_params().metadata.find("has_more");
        if (it_run_has_more != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<bool>(it_run_has_more->second)) {
            server_has_more = std::get<bool>(it_run_has_more->second);
        } else {
            auto it_fields = run_result_summary_obj.raw_params().metadata.find("fields");
            if (it_fields == run_result_summary_obj.raw_params().metadata.end() ||
                (std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second) && std::get<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second) && std::get<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second)->elements.empty())) {
                server_has_more = false;
            }
        }
        std::optional<int64_t> qid_for_pull;
        auto it_qid = run_result_summary_obj.raw_params().metadata.find("qid");
        if (it_qid != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
            qid_for_pull = std::get<int64_t>(it_qid->second);
        }
        while (server_has_more) {
            boltprotocol::PullMessageParams pull_params;
            pull_params.n = -1;
            if (qid_for_pull.has_value() && stream_context_->negotiated_bolt_version >= boltprotocol::versions::V4_3) {
                pull_params.qid = qid_for_pull;
            }
            std::vector<uint8_t> pull_payload_bytes;
            boltprotocol::PackStreamWriter pull_writer(pull_payload_bytes);
            serialize_err = boltprotocol::serialize_pull_message(pull_params, pull_writer);
            if (serialize_err != boltprotocol::BoltError::SUCCESS) {
                last_error_code_ = serialize_err;
                last_error_message_ = "Failed to serialize PULL message: " + error::bolt_error_to_string(serialize_err);
                if (logger) logger->error("[AsyncSessionExec] {}", last_error_message_);
                co_return std::make_pair(
                    last_error_code_,
                    ResultSummary(std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name));
            }
            boltprotocol::BoltError send_pull_err = co_await internal::BoltPhysicalConnection::_send_chunked_payload_async_static_helper(*stream_context_, pull_payload_bytes, stream_context_->original_config, logger, static_op_error_handler);
            if (send_pull_err != boltprotocol::BoltError::SUCCESS) {
                co_return std::make_pair(
                    last_error_code_,
                    ResultSummary(std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name));
            }
            bool current_pull_batch_done = false;
            while (!current_pull_batch_done) {
                auto [recv_err, response_payload] = co_await internal::BoltPhysicalConnection::_receive_chunked_payload_async_static_helper(*stream_context_, stream_context_->original_config, logger, static_op_error_handler);
                if (recv_err != boltprotocol::BoltError::SUCCESS) {
                    co_return std::make_pair(
                        last_error_code_,
                        ResultSummary(std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name));
                }
                if (response_payload.empty()) {
                    if (logger) logger->trace("[AsyncSessionExec] PULL loop received NOOP.");
                    continue;
                }
                boltprotocol::MessageTag tag;
                boltprotocol::PackStreamReader peek_reader_temp(response_payload);
                uint8_t raw_tag_byte_peek = 0;
                uint32_t num_fields_peek = 0;
                boltprotocol::BoltError peek_err = boltprotocol::peek_message_structure_header(peek_reader_temp, raw_tag_byte_peek, num_fields_peek);
                if (peek_err != boltprotocol::BoltError::SUCCESS) {
                    static_op_error_handler(peek_err, "Failed to peek tag in PULL response");
                    co_return std::make_pair(
                        last_error_code_,
                        ResultSummary(std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name));
                }
                tag = static_cast<boltprotocol::MessageTag>(raw_tag_byte_peek);
                boltprotocol::PackStreamReader reader(response_payload);
                if (tag == boltprotocol::MessageTag::RECORD) {
                    boltprotocol::RecordMessageParams rec_params;
                    boltprotocol::BoltError deser_rec_err = boltprotocol::deserialize_record_message(reader, rec_params);
                    if (deser_rec_err != boltprotocol::BoltError::SUCCESS) {
                        static_op_error_handler(deser_rec_err, "Failed to deserialize RECORD in PULL");
                        co_return std::make_pair(
                            last_error_code_,
                            ResultSummary(
                                std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name));
                    }
                    if (logger) logger->trace("[AsyncSessionExec] Consumed a RECORD message.");
                } else if (tag == boltprotocol::MessageTag::SUCCESS) {
                    boltprotocol::SuccessMessageParams pull_summary_meta;
                    boltprotocol::BoltError deser_succ_err = boltprotocol::deserialize_success_message(reader, pull_summary_meta);
                    if (deser_succ_err != boltprotocol::BoltError::SUCCESS) {
                        static_op_error_handler(deser_succ_err, "Failed to deserialize SUCCESS from PULL");
                        co_return std::make_pair(
                            last_error_code_,
                            ResultSummary(
                                std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name));
                    }
                    final_pull_success_meta = std::move(pull_summary_meta);
                    auto it_has_more = final_pull_success_meta.metadata.find("has_more");
                    if (it_has_more != final_pull_success_meta.metadata.end() && std::holds_alternative<bool>(it_has_more->second)) {
                        server_has_more = std::get<bool>(it_has_more->second);
                    } else {
                        server_has_more = false;
                    }
                    current_pull_batch_done = true;
                    if (logger) logger->trace("[AsyncSessionExec] PULL SUCCESS received. HasMore: {}", server_has_more);
                } else if (tag == boltprotocol::MessageTag::FAILURE) {
                    boltprotocol::FailureMessageParams pull_failure_meta;
                    boltprotocol::BoltError deser_fail_err = boltprotocol::deserialize_failure_message(reader, pull_failure_meta);
                    if (deser_fail_err != boltprotocol::BoltError::SUCCESS) {
                        static_op_error_handler(deser_fail_err, "Failed to deserialize FAILURE from PULL");
                    } else {
                        std::string server_fail_detail = error::format_server_failure(pull_failure_meta);
                        static_op_error_handler(boltprotocol::BoltError::UNKNOWN_ERROR, "Server FAILURE during PULL: " + server_fail_detail);
                    }
                    co_return std::make_pair(last_error_code_,
                                             ResultSummary(boltprotocol::SuccessMessageParams{std::move(pull_failure_meta.metadata)},
                                                           stream_context_->negotiated_bolt_version,
                                                           stream_context_->utc_patch_active,
                                                           stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port),
                                                           session_params_.database_name));
                } else {
                    static_op_error_handler(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Unexpected tag " + std::to_string(static_cast<int>(tag)) + " during PULL");
                    co_return std::make_pair(
                        last_error_code_,
                        ResultSummary(std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name));
                }
            }
        }
        last_error_code_ = boltprotocol::BoltError::SUCCESS;
        last_error_message_ = "";
        if (logger) logger->info("[AsyncSessionExec] run_query_async completed successfully for cypher: {:.50}...", cypher);
        ResultSummary final_summary_obj(std::move(final_pull_success_meta), stream_context_->negotiated_bolt_version, stream_context_->utc_patch_active, stream_context_->original_config.target_host + ":" + std::to_string(stream_context_->original_config.target_port), session_params_.database_name);
        co_return std::make_pair(boltprotocol::BoltError::SUCCESS, std::move(final_summary_obj));
    }

    // --- AsyncSessionHandle::run_query_stream_async ---
    boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::unique_ptr<AsyncResultStream>>> AsyncSessionHandle::run_query_stream_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (!is_valid() || !stream_context_) {
            std::string err_msg = "AsyncSessionHandle::run_query_stream_async called on invalid or closed session.";
            if (logger) logger->warn("[AsyncSessionExecStream] {}", err_msg);
            co_return std::make_pair(boltprotocol::BoltError::NETWORK_ERROR, nullptr);
        }
        // Prevent starting new operations if close has been initiated.
        if (close_initiated_.load(std::memory_order_acquire)) {
            std::string err_msg = "AsyncSessionHandle::run_query_stream_async called after close_async initiated.";
            if (logger) logger->warn("[AsyncSessionExecStream] {}", err_msg);
            co_return std::make_pair(boltprotocol::BoltError::INVALID_ARGUMENT, nullptr);
        }

        if (logger) logger->debug("[AsyncSessionExecStream] run_query_stream_async: Cypher: {:.50}...", cypher);

        // 1. Prepare RUN message
        boltprotocol::RunMessageParams run_params = _prepare_run_message_params(cypher, parameters);
        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_writer(run_payload_bytes);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_run_message(run_params, run_writer, stream_context_->negotiated_bolt_version);

        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize RUN message (stream): " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionExecStream] {}", last_error_message_);
            co_return std::make_pair(last_error_code_, nullptr);
        }

        auto static_op_error_handler_for_stream = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            // This handler primarily sets the session's error state.
            // The AsyncResultStream will observe this or its own errors.
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            // Do NOT call mark_closed() here, as the stream might be recoverable or the error is specific to the operation
            if (logger_copy) logger_copy->error("[AsyncSessionExecStream:StaticOpErrHandler] Error: {} - {}", static_cast<int>(reason), message);
        };

        // 2. Send RUN and get initial SUCCESS (fields) or FAILURE.
        // We use send_request_receive_summary_async_static because RUN itself is a request-summary exchange.
        // Pipelining of RECORDs after RUN's SUCCESS is handled by Bolt protocol, not by this specific call.
        auto [run_summary_err, run_result_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, run_payload_bytes, stream_context_->original_config, logger, static_op_error_handler_for_stream);

        if (run_summary_err != boltprotocol::BoltError::SUCCESS) {
            // Error already logged by handler or static method.
            // last_error_code_ and message should be set.
            co_return std::make_pair(last_error_code_, nullptr);
        }

        // Successfully sent RUN and received its SUCCESS summary.
        // Now, prepare to hand over to AsyncResultStream.

        std::shared_ptr<std::vector<std::string>> fields_ptr = std::make_shared<std::vector<std::string>>();
        auto it_fields = run_result_summary_obj.raw_params().metadata.find("fields");
        if (it_fields != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(it_fields->second)) {
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

        std::optional<int64_t> qid_for_stream;
        auto it_qid = run_result_summary_obj.raw_params().metadata.find("qid");
        if (it_qid != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
            qid_for_stream = std::get<int64_t>(it_qid->second);
        }

        bool server_had_more_after_run = true;  // Default assumption
        auto it_run_has_more = run_result_summary_obj.raw_params().metadata.find("has_more");
        if (it_run_has_more != run_result_summary_obj.raw_params().metadata.end() && std::holds_alternative<bool>(it_run_has_more->second)) {
            server_had_more_after_run = std::get<bool>(it_run_has_more->second);
        } else {                        // If "has_more" is not present in RUN's SUCCESS
            if (fields_ptr->empty()) {  // If no fields were returned, there are no records.
                server_had_more_after_run = false;
            }
            // Otherwise, assume server might send records if fields are present.
        }

        // We move the stream_context_ ownership to the AsyncResultStream.
        // This AsyncSessionHandle instance becomes "invalid" for further stream operations until
        // a new stream_context_ is acquired (e.g., for a new query after this stream is done).
        // However, an AsyncSessionHandle is typically for one logical operation sequence.
        // If we want the AsyncSessionHandle to be reusable for more queries *after* an AsyncResultStream
        // is consumed/closed, the stream_context_ ownership model needs to be different (e.g., shared or passed back).
        // For now, moving ownership to AsyncResultStream is simpler and aligns with a single-stream-per-handle model.
        // This means this AsyncSessionHandle should not be used for other queries once run_query_stream_async is called
        // and returns a valid stream, UNLESS the AsyncResultStream returns ownership of the stream_context_ upon its destruction/closure.

        // The AsyncSessionHandle itself is not closed here, but its stream_context_ is given away.
        // It can be "re-armed" if _acquire_active_async_stream_context is called again.
        // But for a typical async flow, one might get a new AsyncSessionHandle per logical query sequence.

        std::unique_ptr<AsyncResultStream> result_stream = std::make_unique<AsyncResultStream>(this,                        // Pass non-owning pointer to this session
                                                                                               std::move(stream_context_),  // Move ownership of stream context to the result stream
                                                                                               qid_for_stream,
                                                                                               run_result_summary_obj.raw_params(),  // Pass RUN raw summary
                                                                                               fields_ptr,
                                                                                               std::vector<boltprotocol::RecordMessageParams>{},  // No pipelined records assumed for this initial step
                                                                                               server_had_more_after_run,
                                                                                               this->session_params_  // Pass session config for fetch_size etc.
        );

        // After moving stream_context_, this AsyncSessionHandle instance should not attempt further operations on it.
        // Its is_valid() will return false.

        if (logger) logger->info("[AsyncSessionExecStream] AsyncResultStream created. QID: {}. Fields: {}", qid_for_stream.has_value() ? std::to_string(qid_for_stream.value()) : "N/A", fields_ptr->size());
        co_return std::make_pair(boltprotocol::BoltError::SUCCESS, std::move(result_stream));
    }

}  // namespace neo4j_bolt_transport