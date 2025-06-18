#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::begin_transaction_async(const std::optional<AsyncTransactionConfigOverrides>& tx_config) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (!is_valid() || !stream_context_) {
            if (logger) logger->warn("[AsyncSessionTXCtrl] begin_transaction_async on invalid session.");
            co_return boltprotocol::BoltError::NETWORK_ERROR;
        }
        if (in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncSessionTXCtrl] begin_transaction_async: Already in an explicit transaction.");
            co_return boltprotocol::BoltError::INVALID_ARGUMENT;
        }

        boltprotocol::BeginMessageParams begin_params = _prepare_begin_message_params(tx_config);  // Uses helper
        std::vector<uint8_t> begin_payload;
        boltprotocol::PackStreamWriter writer(begin_payload);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_begin_message(begin_params, writer, stream_context_->negotiated_bolt_version);

        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize BEGIN message: " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionTXCtrl] {}", last_error_message_);
            co_return last_error_code_;
        }

        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionTXCtrl:StaticOpErrHandler] BEGIN Error: {} - {}", static_cast<int>(reason), message);
        };

        if (logger) logger->debug("[AsyncSessionTXCtrl] Sending BEGIN message. DB: {}, Bookmarks: {}", begin_params.db.value_or("<default>"), begin_params.bookmarks.has_value() ? std::to_string(begin_params.bookmarks->size()) : "0");

        auto [summary_err, begin_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, begin_payload, stream_context_->original_config, logger, static_op_error_handler);

        if (summary_err != boltprotocol::BoltError::SUCCESS) {
            co_return last_error_code_;
        }

        in_explicit_transaction_.store(true, std::memory_order_release);
        last_tx_run_qid_.reset();
        last_error_code_ = boltprotocol::BoltError::SUCCESS;
        last_error_message_ = "";
        if (logger) logger->info("[AsyncSessionTXCtrl] Asynchronous transaction started.");
        co_return boltprotocol::BoltError::SUCCESS;
    }

    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::commit_transaction_async() {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (!is_valid() || !stream_context_) {
            if (logger) logger->warn("[AsyncSessionTXCtrl] commit_transaction_async on invalid session.");
            co_return boltprotocol::BoltError::NETWORK_ERROR;
        }
        if (!in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncSessionTXCtrl] commit_transaction_async: Not in an explicit transaction.");
            co_return boltprotocol::BoltError::INVALID_ARGUMENT;
        }

        std::vector<uint8_t> commit_payload;
        boltprotocol::PackStreamWriter writer(commit_payload);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_commit_message(writer);
        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            last_error_code_ = serialize_err;
            last_error_message_ = "Failed to serialize COMMIT: " + error::bolt_error_to_string(serialize_err);
            if (logger) logger->error("[AsyncSessionTXCtrl] {}", last_error_message_);
            in_explicit_transaction_.store(false, std::memory_order_release);
            last_tx_run_qid_.reset();
            co_return last_error_code_;
        }

        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionTXCtrl:StaticOpErrHandler] COMMIT Error: {} - {}", static_cast<int>(reason), message);
        };

        if (logger) logger->debug("[AsyncSessionTXCtrl] Sending COMMIT message.");

        auto [summary_err, commit_summary_obj] =  // commit_summary_obj is ResultSummary
            co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, commit_payload, stream_context_->original_config, logger, static_op_error_handler);

        in_explicit_transaction_.store(false, std::memory_order_release);
        last_tx_run_qid_.reset();

        if (summary_err != boltprotocol::BoltError::SUCCESS) {
            co_return last_error_code_;
        }

        _update_bookmarks_from_summary(commit_summary_obj.raw_params());
        last_error_code_ = boltprotocol::BoltError::SUCCESS;
        last_error_message_ = "";
        if (logger) logger->info("[AsyncSessionTXCtrl] Asynchronous transaction committed. Last bookmarks: {}", current_bookmarks_.empty() ? "<none>" : current_bookmarks_[0]);
        co_return boltprotocol::BoltError::SUCCESS;
    }

    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::rollback_transaction_async() {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (!is_valid() || !stream_context_) {
            if (logger) logger->warn("[AsyncSessionTXCtrl] rollback_transaction_async on invalid session.");
            in_explicit_transaction_.store(false, std::memory_order_release);
            last_tx_run_qid_.reset();
            co_return boltprotocol::BoltError::NETWORK_ERROR;
        }
        if (!in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->trace("[AsyncSessionTXCtrl] rollback_transaction_async: Not in an explicit transaction. No-op.");
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
            if (logger) logger->error("[AsyncSessionTXCtrl] {}", last_error_message_);
            co_return last_error_code_;
        }

        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->last_error_code_ = reason;
            this->last_error_message_ = message;
            if (logger_copy) logger_copy->error("[AsyncSessionTXCtrl:StaticOpErrHandler] ROLLBACK Error: {} - {}", static_cast<int>(reason), message);
        };

        if (logger) logger->debug("[AsyncSessionTXCtrl] Sending ROLLBACK message.");

        auto [summary_err, rollback_summary_obj] = co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, rollback_payload, stream_context_->original_config, logger, static_op_error_handler);

        if (summary_err != boltprotocol::BoltError::SUCCESS) {
            co_return last_error_code_;
        }

        last_error_code_ = boltprotocol::BoltError::SUCCESS;
        last_error_message_ = "";
        if (logger) logger->info("[AsyncSessionTXCtrl] Asynchronous transaction rolled back.");
        co_return boltprotocol::BoltError::SUCCESS;
    }

}  // namespace neo4j_bolt_transport