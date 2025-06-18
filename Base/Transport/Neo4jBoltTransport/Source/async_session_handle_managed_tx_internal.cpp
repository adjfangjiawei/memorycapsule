#include <algorithm>  // For std::min
#include <boost/asio/steady_timer.hpp>
#include <chrono>

#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    boost::asio::awaitable<TransactionWorkResult> AsyncSessionHandle::_execute_transaction_work_internal_async(AsyncTransactionWork work, config::AccessMode mode_hint, const std::optional<AsyncTransactionConfigOverrides>& tx_config) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        if (is_closed_.load(std::memory_order_acquire) || !stream_context_) {
            if (logger) logger->warn("[AsyncSessionManagedTXInternal] Session is closed or stream context invalid, cannot execute async transaction work.");
            co_return TransactionWorkResult{boltprotocol::BoltError::INVALID_ARGUMENT, "Session is closed or stream context invalid."};
        }
        if (in_explicit_transaction_.load(std::memory_order_acquire)) {
            if (logger) logger->warn("[AsyncSessionManagedTXInternal] Cannot start managed async transaction; an explicit transaction is already active.");
            co_return TransactionWorkResult{boltprotocol::BoltError::INVALID_ARGUMENT, "Explicit transaction already active."};
        }

        uint32_t max_retry_time_ms = 30000;
        uint32_t current_delay_ms = 1000;
        uint32_t max_delay_ms = 60000;
        double multiplier = 2.0;

        if (transport_manager_) {
            const auto& driver_conf = transport_manager_->get_config();
            max_retry_time_ms = driver_conf.max_transaction_retry_time_ms;
            current_delay_ms = driver_conf.transaction_retry_delay_initial_ms > 0 ? driver_conf.transaction_retry_delay_initial_ms : 1000;
            max_delay_ms = driver_conf.transaction_retry_delay_max_ms > 0 ? driver_conf.transaction_retry_delay_max_ms : 60000;
            multiplier = driver_conf.transaction_retry_delay_multiplier > 1.0 ? static_cast<double>(driver_conf.transaction_retry_delay_multiplier) : 2.0;
        }

        auto overall_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_retry_time_ms);
        TransactionWorkResult last_attempt_result = {boltprotocol::BoltError::UNKNOWN_ERROR, "Async transaction work did not complete successfully within retry budget."};
        int attempt_count = 0;

        config::AccessMode original_session_access_mode = session_params_.default_access_mode;
        session_params_.default_access_mode = mode_hint;

        while (std::chrono::steady_clock::now() < overall_deadline) {
            attempt_count++;
            if (logger) logger->debug("[AsyncSessionManagedTXInternal][Attempt {}] Starting async transaction work (Mode: {}).", attempt_count, (mode_hint == config::AccessMode::READ ? "READ" : "WRITE"));

            if (!is_valid() || !stream_context_) {
                last_attempt_result = {boltprotocol::BoltError::NETWORK_ERROR, "Managed async TX: Connection unavailable before BEGIN (Attempt " + std::to_string(attempt_count) + ")"};
                if (logger) logger->warn("[AsyncSessionManagedTXInternal] {}", last_attempt_result.second);
                session_params_.default_access_mode = original_session_access_mode;
                co_return last_attempt_result;
            }

            boltprotocol::BoltError begin_err = co_await begin_transaction_async(tx_config);
            if (begin_err != boltprotocol::BoltError::SUCCESS) {
                bool is_retryable_begin_failure = (begin_err == boltprotocol::BoltError::NETWORK_ERROR || !is_valid());
                last_attempt_result = {begin_err, "Managed async TX: Failed to begin (Attempt " + std::to_string(attempt_count) + "): " + last_error_message_};
                if (logger) logger->warn("[AsyncSessionManagedTXInternal] {}", last_attempt_result.second);

                if (is_retryable_begin_failure && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (logger) logger->info("[AsyncSessionManagedTXInternal] Retrying BEGIN in {}ms.", current_delay_ms);
                    boost::asio::steady_timer retry_timer(stream_context_->get_executor());
                    retry_timer.expires_after(std::chrono::milliseconds(current_delay_ms));
                    co_await retry_timer.async_wait(boost::asio::use_awaitable);
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                    continue;
                }
                session_params_.default_access_mode = original_session_access_mode;
                co_return last_attempt_result;
            }

            AsyncTransactionContext tx_ctx(*this);
            TransactionWorkResult work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "User async work lambda not called or did not complete."};

            try {
                work_res = co_await work(tx_ctx);
            } catch (const boost::system::system_error& e) {
                work_res = {boltprotocol::BoltError::NETWORK_ERROR, "System error from user async work: " + std::string(e.what())};
                if (logger) logger->error("[AsyncSessionManagedTXInternal] System error in user async work: {}", e.what());
            } catch (const std::exception& e) {
                work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "Exception from user async work: " + std::string(e.what())};
                if (logger) logger->error("[AsyncSessionManagedTXInternal] Exception in user async work: {}", e.what());
            } catch (...) {
                work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "Unknown exception from user async work."};
                if (logger) logger->error("[AsyncSessionManagedTXInternal] Unknown exception in user async work.");
            }

            if (!is_valid() || (stream_context_ && !std::visit(
                                                       [](auto& s) {
                                                           return s.lowest_layer().is_open();
                                                       },
                                                       stream_context_->stream))) {
                if (logger) logger->warn("[AsyncSessionManagedTXInternal] Connection became invalid during user async work.");
                work_res = {boltprotocol::BoltError::NETWORK_ERROR, "Connection lost during async transaction work."};
            }

            if (work_res.first == boltprotocol::BoltError::SUCCESS) {
                boltprotocol::BoltError commit_err = co_await commit_transaction_async();
                if (commit_err == boltprotocol::BoltError::SUCCESS) {
                    if (logger) logger->info("[AsyncSessionManagedTXInternal] Async transaction work committed successfully.");
                    session_params_.default_access_mode = original_session_access_mode;
                    co_return TransactionWorkResult{boltprotocol::BoltError::SUCCESS, ""};
                } else {
                    last_attempt_result = {commit_err, "Managed async TX: Commit failed (Attempt " + std::to_string(attempt_count) + "): " + last_error_message_};
                    if (logger) logger->warn("[AsyncSessionManagedTXInternal] {}", last_attempt_result.second);
                    bool is_commit_retryable = (commit_err == boltprotocol::BoltError::NETWORK_ERROR || !is_valid());
                    if (is_commit_retryable && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                        if (logger) logger->info("[AsyncSessionManagedTXInternal] COMMIT failed retryable, retrying whole TX in {}ms.", current_delay_ms);
                        boost::asio::steady_timer retry_timer(stream_context_->get_executor());
                        retry_timer.expires_after(std::chrono::milliseconds(current_delay_ms));
                        co_await retry_timer.async_wait(boost::asio::use_awaitable);
                        current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                        continue;
                    }
                    session_params_.default_access_mode = original_session_access_mode;
                    co_return last_attempt_result;
                }
            } else {
                boltprotocol::BoltError rollback_err = co_await rollback_transaction_async();
                if (rollback_err != boltprotocol::BoltError::SUCCESS && is_valid() && logger) {  // is_valid check for logger
                    logger->warn("[AsyncSessionManagedTXInternal] Rollback failed after work error ('{}'): {}", work_res.second, last_error_message_);
                }
                last_attempt_result = work_res;
                if (logger) logger->warn("[AsyncSessionManagedTXInternal] Work failed (Attempt {}): {}", attempt_count, work_res.second);

                bool is_work_error_retryable = (work_res.first == boltprotocol::BoltError::NETWORK_ERROR || !is_valid());
                if (is_work_error_retryable && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (logger) logger->info("[AsyncSessionManagedTXInternal] Work failed retryable, retrying whole TX in {}ms.", current_delay_ms);
                    boost::asio::steady_timer retry_timer(stream_context_->get_executor());
                    retry_timer.expires_after(std::chrono::milliseconds(current_delay_ms));
                    co_await retry_timer.async_wait(boost::asio::use_awaitable);
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                    continue;
                }
                session_params_.default_access_mode = original_session_access_mode;
                co_return last_attempt_result;
            }
        }

        if (logger) {
            logger->warn("[AsyncSessionManagedTXInternal] Async transaction work failed after all {} retries or timeout. Last error: {}", attempt_count, last_attempt_result.second);
        }
        session_params_.default_access_mode = original_session_access_mode;
        co_return last_attempt_result;
    }

}  // namespace neo4j_bolt_transport