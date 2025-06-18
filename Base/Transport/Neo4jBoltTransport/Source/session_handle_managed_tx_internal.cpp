#include <algorithm>  // For std::min
#include <chrono>     // For std::chrono::milliseconds
#include <iostream>   // 调试用
#include <thread>     // For std::this_thread::sleep_for
#include <utility>    // For std::move

#include "neo4j_bolt_transport/config/transport_config.h"  // For retry config access
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"       // For transport_manager_ to get config
#include "neo4j_bolt_transport/neo4j_transaction_context.h"  // For TransactionContext definition
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    TransactionWorkResult SessionHandle::_execute_transaction_work_internal(TransactionWork work, config::AccessMode mode_hint, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout_opt) {
        std::shared_ptr<spdlog::logger> drv_logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {  // Check transport_manager_
            drv_logger = transport_manager_->get_config().logger;
        }

        if (is_closed()) {
            if (drv_logger) drv_logger->warn("[SessionTX Managed] Session is closed, cannot execute transaction work.");
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Session is closed, cannot execute transaction work."};
        }
        if (in_explicit_transaction_) {
            if (drv_logger) drv_logger->warn("[SessionTX Managed] Cannot start managed transaction; an explicit transaction is already active.");
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Cannot start managed transaction; an explicit transaction is already active."};
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
            multiplier = driver_conf.transaction_retry_delay_multiplier > 1 ? static_cast<double>(driver_conf.transaction_retry_delay_multiplier) : 2.0;
        }

        auto overall_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_retry_time_ms);
        TransactionWorkResult last_attempt_result = {boltprotocol::BoltError::UNKNOWN_ERROR, "Transaction work did not complete successfully within retry budget."};
        int attempt_count = 0;

        config::AccessMode original_session_access_mode = session_params_.default_access_mode;
        session_params_.default_access_mode = mode_hint;

        while (std::chrono::steady_clock::now() < overall_deadline) {
            attempt_count++;
            std::shared_ptr<spdlog::logger> current_op_logger = drv_logger;

            std::pair<boltprotocol::BoltError, std::string> conn_check_for_log;
            internal::BoltPhysicalConnection* temp_conn_for_log_check = _get_valid_connection_for_operation(conn_check_for_log, "managed_tx_log_setup");
            if (temp_conn_for_log_check && temp_conn_for_log_check->get_logger()) {
                current_op_logger = temp_conn_for_log_check->get_logger();
            }

            if (current_op_logger) {
                current_op_logger->debug("[SessionTX Managed][Attempt {}] Starting transaction work (Mode: {}).", attempt_count, (mode_hint == config::AccessMode::READ ? "READ" : "WRITE"));
            }

            std::pair<boltprotocol::BoltError, std::string> pre_begin_conn_check;
            if (!_get_valid_connection_for_operation(pre_begin_conn_check, "managed_tx_pre_begin")) {
                last_attempt_result = {pre_begin_conn_check.first, "Managed TX: Connection unavailable before BEGIN (Attempt " + std::to_string(attempt_count) + "): " + pre_begin_conn_check.second};
                if (current_op_logger) current_op_logger->warn("[SessionTX Managed] {}", last_attempt_result.second);

                bool is_retryable_failure = (pre_begin_conn_check.first == boltprotocol::BoltError::NETWORK_ERROR || pre_begin_conn_check.first == boltprotocol::BoltError::HANDSHAKE_FAILED);  // More specific retry conditions
                if (is_retryable_failure && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (current_op_logger) current_op_logger->info("[SessionTX Managed] Retrying entire transaction due to connection unavailability before BEGIN in {}ms.", current_delay_ms);
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                    // If connection was invalidated, _release_connection_to_pool would have been called.
                    // Next iteration _get_valid_connection_for_operation will try to get a new one if pool manager is used.
                    continue;
                }
                session_params_.default_access_mode = original_session_access_mode;
                return last_attempt_result;
            }

            auto begin_res = begin_transaction(tx_metadata, tx_timeout_opt);
            if (begin_res.first != boltprotocol::BoltError::SUCCESS) {
                bool is_retryable_begin_failure = (begin_res.first == boltprotocol::BoltError::NETWORK_ERROR || !connection_is_valid_);
                last_attempt_result = {begin_res.first, "Managed TX: Failed to begin (Attempt " + std::to_string(attempt_count) + "): " + begin_res.second};
                if (current_op_logger) current_op_logger->warn("[SessionTX Managed] {}", last_attempt_result.second);

                if (is_retryable_begin_failure && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (current_op_logger) current_op_logger->info("[SessionTX Managed] Retrying BEGIN in {}ms.", current_delay_ms);
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                    continue;
                }
                session_params_.default_access_mode = original_session_access_mode;
                return last_attempt_result;
            }

            if (connection_ && connection_->get_logger()) current_op_logger = connection_->get_logger();

            TransactionContext tx_context(*this);
            TransactionWorkResult work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "User work lambda not called."};

            try {
                work_res = work(tx_context);
            } catch (const std::exception& e) {
                work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "Exception from user transaction work: " + std::string(e.what())};
                if (current_op_logger) current_op_logger->error("[SessionTX Managed] Exception in user work: {}", e.what());
            } catch (...) {
                work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "Unknown exception from user transaction work."};
                if (current_op_logger) current_op_logger->error("[SessionTX Managed] Unknown exception in user work.");
            }

            if (!connection_is_valid_) {
                if (current_op_logger) current_op_logger->warn("[SessionTX Managed] Connection became invalid during user work. Last conn error code: {}", connection_ ? static_cast<int>(connection_->get_last_error_code()) : -1);
                work_res = {boltprotocol::BoltError::NETWORK_ERROR, "Connection lost during transaction work execution."};
                rollback_transaction();
                last_attempt_result = work_res;
                if (std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (current_op_logger) current_op_logger->info("[SessionTX Managed] Retrying entire transaction due to connection loss in {}ms.", current_delay_ms);
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                    continue;
                } else {
                    session_params_.default_access_mode = original_session_access_mode;
                    return last_attempt_result;
                }
            }

            if (work_res.first == boltprotocol::BoltError::SUCCESS) {
                auto commit_res = commit_transaction();
                if (commit_res.first == boltprotocol::BoltError::SUCCESS) {
                    if (current_op_logger) current_op_logger->info("[SessionTX Managed] Transaction work committed successfully.");
                    session_params_.default_access_mode = original_session_access_mode;
                    return {boltprotocol::BoltError::SUCCESS, ""};
                } else {
                    last_attempt_result = {commit_res.first, "Managed TX: Commit failed (Attempt " + std::to_string(attempt_count) + "): " + commit_res.second};
                    if (current_op_logger) current_op_logger->warn("[SessionTX Managed] {}", last_attempt_result.second);
                    bool is_commit_retryable = (commit_res.first == boltprotocol::BoltError::NETWORK_ERROR || !connection_is_valid_);
                    // Potentially check for specific Neo4j error codes from commit_res.second if available
                    if (is_commit_retryable && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                        if (current_op_logger) current_op_logger->info("[SessionTX Managed] COMMIT failed retryable, retrying whole TX in {}ms.", current_delay_ms);
                        std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                        current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                        continue;
                    }
                    session_params_.default_access_mode = original_session_access_mode;
                    return last_attempt_result;
                }
            } else {  // User lambda returned error
                auto rollback_res = rollback_transaction();
                if (rollback_res.first != boltprotocol::BoltError::SUCCESS && connection_is_valid_ && current_op_logger) {
                    current_op_logger->warn("[SessionTX Managed] Rollback failed after work error ('{}'): {}", work_res.second, rollback_res.second);
                }
                last_attempt_result = work_res;
                if (current_op_logger) current_op_logger->warn("[SessionTX Managed] Work failed (Attempt {}): {}", attempt_count, work_res.second);
                bool is_work_error_retryable = (work_res.first == boltprotocol::BoltError::NETWORK_ERROR || !connection_is_valid_);
                // Potentially check for specific Neo4j error codes for retry
                if (is_work_error_retryable && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (current_op_logger) current_op_logger->info("[SessionTX Managed] Work failed retryable, retrying whole TX in {}ms.", current_delay_ms);
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                    continue;
                }
                session_params_.default_access_mode = original_session_access_mode;
                return last_attempt_result;
            }
        }

        if (drv_logger) {
            drv_logger->warn("[SessionTX Managed] Transaction work failed after all {} retries or timeout. Last error: {}", attempt_count, last_attempt_result.second);
        }
        session_params_.default_access_mode = original_session_access_mode;
        return last_attempt_result;
    }

}  // namespace neo4j_bolt_transport