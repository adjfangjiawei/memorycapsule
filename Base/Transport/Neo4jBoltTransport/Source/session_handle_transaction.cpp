#include <chrono>    // For std::chrono::milliseconds
#include <iostream>  // For debug logging, replace with proper logging
#include <thread>    // For std::this_thread::sleep_for

#include "neo4j_bolt_transport/config/transport_config.h"  // For retry config access
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"  // For transport_manager_ to get config
#include "neo4j_bolt_transport/neo4j_transaction_context.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    // --- Explicit Transaction Methods ---
    std::pair<boltprotocol::BoltError, std::string> SessionHandle::begin_transaction(const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        std::pair<boltprotocol::BoltError, std::string> conn_check_result;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_result, "begin_transaction");
        if (!conn) {
            return conn_check_result;
        }
        auto logger = conn->get_logger();  // Get logger from connection

        if (in_explicit_transaction_) {
            if (logger) logger->warn("[SessionTX {}] Attempt to begin transaction while already in one.", conn->get_id());
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Cannot begin transaction; already in an explicit transaction."};
        }

        boltprotocol::BeginMessageParams params;
        params.bookmarks = current_bookmarks_;
        if (session_params_.database_name.has_value()) {
            params.db = session_params_.database_name;
        }
        if (session_params_.impersonated_user.has_value()) {
            params.imp_user = session_params_.impersonated_user;
        }

        if (!(conn->get_bolt_version() < boltprotocol::versions::V5_0)) {  // Bolt 5.0+
            if (session_params_.default_access_mode == config::AccessMode::READ) {
                params.other_extra_fields["mode"] = std::string("r");
            }
        }

        if (tx_metadata.has_value()) {
            params.tx_metadata = *tx_metadata;
        }
        if (tx_timeout.has_value()) {
            params.tx_timeout = tx_timeout.value().count();
        }

        std::vector<uint8_t> begin_payload;
        boltprotocol::PackStreamWriter writer(begin_payload);
        boltprotocol::BoltError err_code = boltprotocol::serialize_begin_message(params, writer, conn->get_bolt_version());
        if (err_code != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("BEGIN serialization", err_code);
            _invalidate_session_due_to_connection_error(err_code, msg);
            return {err_code, msg};
        }

        boltprotocol::SuccessMessageParams success_meta;
        boltprotocol::FailureMessageParams failure_meta;
        err_code = conn->send_request_receive_summary(begin_payload, success_meta, failure_meta);

        if (err_code == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error() == boltprotocol::BoltError::SUCCESS) {
                in_explicit_transaction_ = true;
                current_transaction_query_id_.reset();
                if (logger) {
                    logger->info("[SessionTX {}] Transaction started.", conn->get_id());
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string server_fail_msg = error::format_server_failure(failure_meta);
                std::string msg = error::format_error_message("BEGIN failed on server", conn->get_last_error(), server_fail_msg);
                _invalidate_session_due_to_connection_error(conn->get_last_error(), msg);
                return {conn->get_last_error(), msg};
            }
        }
        std::string msg = error::format_error_message("BEGIN send/receive", err_code, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err_code, msg);
        return {err_code, msg};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::commit_transaction() {
        std::pair<boltprotocol::BoltError, std::string> conn_check_result;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_result, "commit_transaction");
        if (!conn) return conn_check_result;
        auto logger = conn->get_logger();

        if (!in_explicit_transaction_) {
            if (logger) logger->warn("[SessionTX {}] Attempt to commit transaction while not in one.", conn->get_id());
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Cannot commit: not in an explicit transaction."};
        }

        std::vector<uint8_t> commit_payload;
        boltprotocol::PackStreamWriter writer(commit_payload);
        boltprotocol::BoltError err_code = boltprotocol::serialize_commit_message(writer);
        if (err_code != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("COMMIT serialization", err_code);
            _invalidate_session_due_to_connection_error(err_code, msg);
            in_explicit_transaction_ = false;
            current_transaction_query_id_.reset();
            return {err_code, msg};
        }

        boltprotocol::SuccessMessageParams success_meta;
        boltprotocol::FailureMessageParams failure_meta;
        err_code = conn->send_request_receive_summary(commit_payload, success_meta, failure_meta);

        in_explicit_transaction_ = false;
        current_transaction_query_id_.reset();

        if (err_code == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error() == boltprotocol::BoltError::SUCCESS) {
                auto it_bookmark = success_meta.metadata.find("bookmark");
                if (it_bookmark != success_meta.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                    current_bookmarks_ = {std::get<std::string>(it_bookmark->second)};
                } else {
                    current_bookmarks_.clear();
                }
                if (logger) {
                    logger->info("[SessionTX {}] Transaction committed.", conn->get_id());
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string msg = error::format_error_message("COMMIT failed on server", conn->get_last_error(), error::format_server_failure(failure_meta));
                _invalidate_session_due_to_connection_error(conn->get_last_error(), msg);
                return {conn->get_last_error(), msg};
            }
        }
        std::string msg = error::format_error_message("COMMIT send/receive", err_code, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err_code, msg);
        return {err_code, msg};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::rollback_transaction() {
        std::pair<boltprotocol::BoltError, std::string> conn_check_result;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_result, "rollback_transaction (pre-check)");

        std::shared_ptr<spdlog::logger> logger = conn ? conn->get_logger() : (transport_manager_ ? transport_manager_->get_config().logger : nullptr);

        if (!in_explicit_transaction_) {
            // Not an error to rollback if not in TX, just a no-op.
            return {boltprotocol::BoltError::SUCCESS, ""};
        }
        if (!conn) {  // Was in transaction, but connection died
            std::string msg = "Rollback attempt with no valid connection while in TX: " + conn_check_result.second;
            if (logger) logger->warn("[SessionTX Managed] {}", msg);
            _invalidate_session_due_to_connection_error(conn_check_result.first, msg);
            in_explicit_transaction_ = false;
            current_transaction_query_id_.reset();
            return {conn_check_result.first, msg};
        }
        // Now 'conn' and 'logger' (from conn) are valid if this point is reached.

        std::vector<uint8_t> rollback_payload;
        boltprotocol::PackStreamWriter writer(rollback_payload);
        boltprotocol::BoltError err_code = boltprotocol::serialize_rollback_message(writer);
        if (err_code != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("ROLLBACK serialization", err_code);
            _invalidate_session_due_to_connection_error(err_code, msg);
            in_explicit_transaction_ = false;
            current_transaction_query_id_.reset();
            return {err_code, msg};
        }

        boltprotocol::SuccessMessageParams success_meta;
        boltprotocol::FailureMessageParams failure_meta;
        err_code = conn->send_request_receive_summary(rollback_payload, success_meta, failure_meta);

        in_explicit_transaction_ = false;
        current_transaction_query_id_.reset();

        if (err_code == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error() == boltprotocol::BoltError::SUCCESS) {
                if (logger) {
                    logger->info("[SessionTX {}] Transaction rolled back.", conn->get_id());
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string msg = error::format_error_message("ROLLBACK failed on server", conn->get_last_error(), error::format_server_failure(failure_meta));
                _invalidate_session_due_to_connection_error(conn->get_last_error(), msg);
                return {conn->get_last_error(), msg};
            }
        }
        std::string msg = error::format_error_message("ROLLBACK send/receive", err_code, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err_code, msg);
        return {err_code, msg};
    }

    // --- Managed Transaction Functions ---
    TransactionWorkResult SessionHandle::_execute_transaction_work_internal(TransactionWork work, config::AccessMode /*mode_hint_for_routing_later*/, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        std::shared_ptr<spdlog::logger> drv_logger = transport_manager_ ? transport_manager_->get_config().logger : nullptr;

        if (is_closed()) {
            if (drv_logger) drv_logger->warn("[SessionTX Managed] Session is closed, cannot execute transaction work.");
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Session is closed, cannot execute transaction work."};
        }
        if (in_explicit_transaction_) {
            if (drv_logger) drv_logger->warn("[SessionTX Managed] Cannot start managed transaction; an explicit transaction is already active.");
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Cannot start managed transaction; an explicit transaction is already active."};
        }

        uint32_t max_retry_time_ms = 30000;
        uint32_t current_delay_ms = 1000;  // Default if not configured
        uint32_t max_delay_ms = 60000;     // Default if not configured
        double multiplier = 2.0;           // Default if not configured

        if (transport_manager_) {
            const auto& driver_conf = transport_manager_->get_config();
            max_retry_time_ms = driver_conf.max_transaction_retry_time_ms;
            current_delay_ms = driver_conf.transaction_retry_delay_initial_ms > 0 ? driver_conf.transaction_retry_delay_initial_ms : 1000;
            max_delay_ms = driver_conf.transaction_retry_delay_max_ms > 0 ? driver_conf.transaction_retry_delay_max_ms : 60000;
            multiplier = driver_conf.transaction_retry_delay_multiplier > 1.0 ? driver_conf.transaction_retry_delay_multiplier : 2.0;
        }

        auto overall_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_retry_time_ms);
        TransactionWorkResult last_attempt_result = {boltprotocol::BoltError::UNKNOWN_ERROR, "Transaction work did not complete successfully within retry budget."};
        int attempt_count = 0;

        while (std::chrono::steady_clock::now() < overall_deadline) {
            attempt_count++;
            std::shared_ptr<spdlog::logger> current_conn_logger = nullptr;  // Logger for current connection attempt
            if (connection_ && connection_->get_logger())
                current_conn_logger = connection_->get_logger();
            else if (drv_logger)
                current_conn_logger = drv_logger;  // Fallback to driver logger

            if (current_conn_logger) {
                current_conn_logger->debug("[SessionTX Managed] Attempt #{} for transaction work.", attempt_count);
            }

            auto begin_res = begin_transaction(tx_metadata, tx_timeout);
            if (begin_res.first != boltprotocol::BoltError::SUCCESS) {
                bool is_retryable_begin_failure = (begin_res.first == boltprotocol::BoltError::NETWORK_ERROR);  // Simplified
                last_attempt_result = {begin_res.first, "Managed TX: Failed to begin: " + begin_res.second};

                if (current_conn_logger) current_conn_logger->warn("[SessionTX Managed] BEGIN failed. Error: {}", begin_res.second);

                if (is_retryable_begin_failure && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (current_conn_logger) {
                        current_conn_logger->info("[SessionTX Managed] Retrying BEGIN in {}ms.", current_delay_ms);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms * multiplier), static_cast<double>(max_delay_ms)));
                    // _invalidate_session_due_to_connection_error is called by begin_transaction if conn fails
                    _release_connection_to_pool(false);  // Release bad connection
                    continue;                            // Retry by acquiring a new connection and beginning transaction
                }
                return last_attempt_result;  // Non-retryable BEGIN failure or out of time
            }

            // If begin_transaction succeeded, 'connection_' should be valid. Update current_conn_logger.
            if (connection_ && connection_->get_logger()) current_conn_logger = connection_->get_logger();

            TransactionContext tx_context(*this);  // Correct construction
            TransactionWorkResult work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "User work lambda not called."};

            connection_is_valid_ = true;  // Assume valid after successful BEGIN

            try {
                work_res = work(tx_context);
            } catch (const std::exception& e) {
                work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "Exception from user transaction work: " + std::string(e.what())};
                if (current_conn_logger) current_conn_logger->error("[SessionTX Managed] {}", work_res.second);
            } catch (...) {
                work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "Unknown exception from user transaction work."};
                if (current_conn_logger) current_conn_logger->error("[SessionTX Managed] {}", work_res.second);
            }

            if (!connection_is_valid_) {  // Connection might have died during user's work
                if (current_conn_logger) {
                    current_conn_logger->warn("[SessionTX Managed] Connection became invalid during user work lambda.");
                }
                work_res = {boltprotocol::BoltError::NETWORK_ERROR, "Connection lost during transaction work execution."};
                // Rollback will likely fail but attempt anyway.
                rollback_transaction();  // Best effort
                last_attempt_result = work_res;
                // Fall through to retry logic if applicable
            }

            if (work_res.first == boltprotocol::BoltError::SUCCESS) {
                auto commit_res = commit_transaction();
                if (commit_res.first == boltprotocol::BoltError::SUCCESS) {
                    if (current_conn_logger) {
                        current_conn_logger->info("[SessionTX Managed] Transaction work committed successfully.");
                    }
                    return {boltprotocol::BoltError::SUCCESS, ""};
                } else {  // Commit failed
                    last_attempt_result = {commit_res.first, "Managed TX: Commit failed after successful work: " + commit_res.second};
                    if (current_conn_logger) current_conn_logger->warn("[SessionTX Managed] {}", last_attempt_result.second);

                    bool is_commit_retryable = (commit_res.first == boltprotocol::BoltError::NETWORK_ERROR || !connection_is_valid_);  // Simplified
                    if (is_commit_retryable && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                        if (current_conn_logger) {
                            current_conn_logger->info("[SessionTX Managed] COMMIT failed with retryable error, retrying whole TX in {}ms.", current_delay_ms);
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                        current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms * multiplier), static_cast<double>(max_delay_ms)));
                        _release_connection_to_pool(false);
                        continue;  // Retry the whole transaction
                    }
                    return last_attempt_result;  // Non-retryable COMMIT failure or out of time
                }
            } else {  // User work returned an error or threw an exception
                auto rollback_res = rollback_transaction();
                if (rollback_res.first != boltprotocol::BoltError::SUCCESS && connection_is_valid_ && current_conn_logger) {
                    current_conn_logger->warn("[SessionTX Managed] Rollback failed after work error ('{}'): {}", work_res.second, rollback_res.second);
                }
                last_attempt_result = work_res;
                if (current_conn_logger) current_conn_logger->warn("[SessionTX Managed] Work failed: {}", work_res.second);

                // TODO: Define which BoltError codes from user work are retryable (e.g. Deadlock, specific TransientErrors from server)
                // For now, only network errors or connection invalidity are considered retryable here.
                bool is_work_error_retryable = (work_res.first == boltprotocol::BoltError::NETWORK_ERROR || !connection_is_valid_);

                if (is_work_error_retryable && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (current_conn_logger) {
                        current_conn_logger->info("[SessionTX Managed] Work failed with retryable error, retrying whole TX in {}ms.", current_delay_ms);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms * multiplier), static_cast<double>(max_delay_ms)));
                    if (!connection_is_valid_) {  // If connection died, release it.
                        _release_connection_to_pool(false);
                    }
                    continue;  // Retry whole transaction
                }
                return last_attempt_result;  // Non-retryable work error or out of time
            }
        }
        if (drv_logger) {  // Use driver logger for final summary if available
            drv_logger->warn("[SessionTX Managed] Transaction work failed after all retries or timeout. Last error: {}", last_attempt_result.second);
        }
        return last_attempt_result;
    }

    TransactionWorkResult SessionHandle::execute_read_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        config::AccessMode original_mode = session_params_.default_access_mode;
        session_params_.default_access_mode = config::AccessMode::READ;  // Hint for BEGIN message
        TransactionWorkResult result = _execute_transaction_work_internal(std::move(work), config::AccessMode::READ, tx_metadata, tx_timeout);
        session_params_.default_access_mode = original_mode;  // Restore original mode
        return result;
    }

    TransactionWorkResult SessionHandle::execute_write_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        config::AccessMode original_mode = session_params_.default_access_mode;
        session_params_.default_access_mode = config::AccessMode::WRITE;  // Hint for BEGIN message
        TransactionWorkResult result = _execute_transaction_work_internal(std::move(work), config::AccessMode::WRITE, tx_metadata, tx_timeout);
        session_params_.default_access_mode = original_mode;  // Restore original mode
        return result;
    }

}  // namespace neo4j_bolt_transport