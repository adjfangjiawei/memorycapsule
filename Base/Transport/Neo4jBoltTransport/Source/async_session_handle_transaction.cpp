#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // _prepare_begin_message_params (MODIFIED to include current_bookmarks_)
    boltprotocol::BeginMessageParams AsyncSessionHandle::_prepare_begin_message_params(const std::optional<AsyncTransactionConfigOverrides>& tx_config) {
        boltprotocol::BeginMessageParams begin_p;

        // Use current session bookmarks when starting a new transaction
        if (!current_bookmarks_.empty()) {
            begin_p.bookmarks = current_bookmarks_;
        }

        if (stream_context_) {
            if (session_params_.database_name.has_value()) {
                begin_p.db = session_params_.database_name;
            }
            if (session_params_.impersonated_user.has_value()) {
                begin_p.imp_user = session_params_.impersonated_user;
            }
            if (stream_context_->negotiated_bolt_version >= boltprotocol::versions::V5_0) {
                if (session_params_.default_access_mode == config::AccessMode::READ) {
                    begin_p.mode = "r";
                }
            }

            if (tx_config.has_value()) {
                if (tx_config.value().metadata.has_value()) {
                    begin_p.tx_metadata = tx_config.value().metadata.value();
                }
                if (tx_config.value().timeout.has_value()) {
                    begin_p.tx_timeout = static_cast<int64_t>(tx_config.value().timeout.value().count());
                }
            } else if (transport_manager_ && transport_manager_->get_config().explicit_transaction_timeout_default_ms > 0) {
                begin_p.tx_timeout = static_cast<int64_t>(transport_manager_->get_config().explicit_transaction_timeout_default_ms);
            }
        }
        return begin_p;
    }

    // begin_transaction_async (保持不变)
    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::begin_transaction_async(const std::optional<AsyncTransactionConfigOverrides>& tx_config) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (!is_valid() || !stream_context_) {
            if (logger) logger->warn("[AsyncSessionTX] begin_transaction_async on invalid session.");
            co_return boltprotocol::BoltError::NETWORK_ERROR;
        }
        if (in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncSessionTX] begin_transaction_async: Already in an explicit transaction.");
            co_return boltprotocol::BoltError::INVALID_ARGUMENT;
        }

        boltprotocol::BeginMessageParams begin_params = _prepare_begin_message_params(tx_config);
        std::vector<uint8_t> begin_payload;
        boltprotocol::PackStreamWriter writer(begin_payload);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_begin_message(begin_params, writer, stream_context_->negotiated_bolt_version);

        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize BEGIN message: " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionTX] {}", last_error_message_);
            co_return last_error_code_;
        }

        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionTX:StaticOpErrHandler] BEGIN Error: {} - {}", static_cast<int>(reason), message);
        };

        if (logger) logger->debug("[AsyncSessionTX] Sending BEGIN message. DB: {}, Bookmarks: {}", begin_params.db.value_or("<default>"), begin_params.bookmarks.has_value() ? std::to_string(begin_params.bookmarks->size()) : "0");

        auto [summary_err, begin_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, begin_payload, stream_context_->original_config, logger, static_op_error_handler);

        if (summary_err != boltprotocol::BoltError::SUCCESS) {
            co_return last_error_code_;
        }

        in_explicit_transaction_.store(true, std::memory_order_release);
        last_tx_run_qid_.reset();
        last_error_code_ = boltprotocol::BoltError::SUCCESS;
        last_error_message_ = "";
        if (logger) logger->info("[AsyncSessionTX] Asynchronous transaction started.");
        co_return boltprotocol::BoltError::SUCCESS;
    }

    // commit_transaction_async (MODIFIED to update bookmarks)
    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::commit_transaction_async() {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (!is_valid() || !stream_context_) {
            if (logger) logger->warn("[AsyncSessionTX] commit_transaction_async on invalid session.");
            co_return boltprotocol::BoltError::NETWORK_ERROR;
        }
        if (!in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncSessionTX] commit_transaction_async: Not in an explicit transaction.");
            co_return boltprotocol::BoltError::INVALID_ARGUMENT;
        }

        std::vector<uint8_t> commit_payload;
        boltprotocol::PackStreamWriter writer(commit_payload);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_commit_message(writer);
        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize COMMIT: " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionTX] {}", last_error_message_);
            in_explicit_transaction_.store(false, std::memory_order_release);
            last_tx_run_qid_.reset();
            co_return last_error_code_;
        }

        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionTX:StaticOpErrHandler] COMMIT Error: {} - {}", static_cast<int>(reason), message);
        };

        if (logger) logger->debug("[AsyncSessionTX] Sending COMMIT message.");

        auto [summary_err, commit_summary_obj] =  // commit_summary_obj is ResultSummary
            co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, commit_payload, stream_context_->original_config, logger, static_op_error_handler);

        in_explicit_transaction_.store(false, std::memory_order_release);
        last_tx_run_qid_.reset();

        if (summary_err != boltprotocol::BoltError::SUCCESS) {
            // last_error_code_ should be set by handler or send_request_receive_summary_async_static
            co_return last_error_code_;
        }

        // COMMIT successful, update bookmarks
        _update_bookmarks_from_summary(commit_summary_obj.raw_params());
        last_error_code_ = boltprotocol::BoltError::SUCCESS;
        last_error_message_ = "";
        if (logger) logger->info("[AsyncSessionTX] Asynchronous transaction committed. Last bookmarks: {}", current_bookmarks_.empty() ? "<none>" : current_bookmarks_[0]);
        co_return boltprotocol::BoltError::SUCCESS;
    }

    // rollback_transaction_async (MODIFIED to clear bookmarks)
    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::rollback_transaction_async() {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (!is_valid() || !stream_context_) {
            if (logger) logger->warn("[AsyncSessionTX] rollback_transaction_async on invalid session.");
            in_explicit_transaction_.store(false, std::memory_order_release);
            last_tx_run_qid_.reset();
            co_return boltprotocol::BoltError::NETWORK_ERROR;
        }
        if (!in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->trace("[AsyncSessionTX] rollback_transaction_async: Not in an explicit transaction. No-op.");
            co_return boltprotocol::BoltError::SUCCESS;
        }

        std::vector<uint8_t> rollback_payload;
        boltprotocol::PackStreamWriter writer(rollback_payload);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_rollback_message(writer);

        in_explicit_transaction_.store(false, std::memory_order_release);
        last_tx_run_qid_.reset();

        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize ROLLBACK: " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionTX] {}", last_error_message_);
            co_return last_error_code_;
        }

        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionTX:StaticOpErrHandler] ROLLBACK Error: {} - {}", static_cast<int>(reason), message);
        };

        if (logger) logger->debug("[AsyncSessionTX] Sending ROLLBACK message.");

        auto [summary_err, rollback_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, rollback_payload, stream_context_->original_config, logger, static_op_error_handler);

        if (summary_err != boltprotocol::BoltError::SUCCESS) {
            co_return last_error_code_;
        }

        // After a successful rollback, bookmarks are typically not advanced or changed by the server response.
        // Some drivers might choose to clear session bookmarks after a rollback, others might keep them.
        // For consistency with typical behavior where rollback doesn't yield a new progression point,
        // we do not update bookmarks here from rollback_summary_obj.raw_params().
        // If specific behavior like clearing is desired, it can be added:
        // current_bookmarks_.clear();
        // if (logger) logger->trace("[AsyncSessionTX] Bookmarks cleared after rollback.");

        last_error_code_ = boltprotocol::BoltError::SUCCESS;
        last_error_message_ = "";
        if (logger) logger->info("[AsyncSessionTX] Asynchronous transaction rolled back.");
        co_return boltprotocol::BoltError::SUCCESS;
    }

    // run_query_in_transaction_async (MODIFIED to use current_bookmarks_ in _prepare_run_message_params)
    // Note: _prepare_run_message_params for explicit TX usually doesn't include bookmarks,
    // as they are set at BEGIN. But if it did, current_bookmarks_ would be used.
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
            if (logger) logger->warn("[AsyncSessionTX] run_query_in_transaction_async on invalid session.");
            co_return std::make_pair(boltprotocol::BoltError::NETWORK_ERROR, std::move(default_summary_on_error));
        }
        if (!in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncSessionTX] run_query_in_transaction_async: Not in an explicit transaction.");
            co_return std::make_pair(boltprotocol::BoltError::INVALID_ARGUMENT, std::move(default_summary_on_error));
        }

        if (logger) logger->debug("[AsyncSessionTX] run_query_in_transaction_async: Cypher: {:.50}...", cypher);

        // For RUN in explicit TX, bookmarks/tx_timeout/tx_metadata/mode are NOT part of RunMessageParams 'extra'.
        // They are part of BeginMessageParams.
        boltprotocol::RunMessageParams run_params;
        run_params.cypher_query = cypher;
        run_params.parameters = parameters;
        // run_params.bookmarks should be std::nullopt or empty for RUN in explicit TX.
        // _prepare_run_message_params might need adjustment for this context,
        // or we construct RunMessageParams more directly here.
        // For now, _prepare_run_message_params might add bookmarks if current_bookmarks_ is set,
        // but Bolt spec for RUN in explicit TX: "extra" field is empty.
        // Let's ensure it's empty for explicit TX RUN.
        // A better way is to have _prepare_run_message_params take a bool isInExplicitTx.
        // Quick fix: directly create run_params for explicit TX.

        // Re-creating run_params specifically for explicit TX to ensure 'extra' is minimal/empty
        boltprotocol::RunMessageParams explicit_tx_run_params;
        explicit_tx_run_params.cypher_query = cypher;
        explicit_tx_run_params.parameters = parameters;
        // No bookmarks, tx_timeout, tx_metadata, mode in 'extra' for RUN in explicit TX.

        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_writer(run_payload_bytes);
        // Pass the explicitly constructed params
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_run_message(explicit_tx_run_params, run_writer, stream_context_->negotiated_bolt_version);
        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize RUN (in TX): " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionTX] {}", last_error_message_);
            co_return std::make_pair(last_error_code_, std::move(default_summary_on_error));
        }

        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionTX:StaticOpErrHandler] RUN (in TX) Error: {} - {}", static_cast<int>(reason), message);
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
                if (logger) logger->trace("[AsyncSessionTX] RUN (in TX) got qid: {}", *last_tx_run_qid_);
            } else if (logger) {
                logger->trace("[AsyncSessionTX] RUN (in TX) SUCCESS did not contain 'qid'.");
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
            logger->trace("[AsyncSessionTX] RUN (in TX) indicates no records to PULL.");
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
                if (logger) logger->error("[AsyncSessionTX] {}", last_error_message_);
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
                    if (logger) logger->trace("[AsyncSessionTX] PULL (in TX) loop received NOOP.");
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
                    if (logger) logger->trace("[AsyncSessionTX] Consumed a RECORD message (in TX).");
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
                    if (logger) logger->trace("[AsyncSessionTX] PULL (in TX) SUCCESS received. HasMore: {}", server_has_more_pull);

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
        if (logger) logger->info("[AsyncSessionTX] run_query_in_transaction_async successful for: {:.50}...", cypher);
        co_return std::make_pair(boltprotocol::BoltError::SUCCESS, std::move(final_summary_for_tx_run));
    }

}  // namespace neo4j_bolt_transport