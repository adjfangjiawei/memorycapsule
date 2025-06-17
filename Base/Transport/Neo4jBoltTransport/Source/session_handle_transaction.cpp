#include <algorithm>  // For std::min
#include <chrono>     // For std::chrono::milliseconds
#include <iostream>   // 调试用
#include <thread>     // For std::this_thread::sleep_for
#include <utility>    // For std::move

#include "boltprotocol/message_serialization.h"            // For serialize_..._message
#include "boltprotocol/packstream_writer.h"                // For PackStreamWriter
#include "neo4j_bolt_transport/config/transport_config.h"  // For retry config access
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"       // For transport_manager_ to get config
#include "neo4j_bolt_transport/neo4j_transaction_context.h"  // For TransactionContext definition
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    // --- Explicit Transaction Methods ---
    std::pair<boltprotocol::BoltError, std::string> SessionHandle::begin_transaction(const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        std::pair<boltprotocol::BoltError, std::string> conn_check_result;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_result, "begin_transaction");
        if (!conn) {
            return conn_check_result;
        }
        auto logger = conn->get_logger();

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

        if (!(conn->get_bolt_version() < boltprotocol::versions::Version(5, 0))) {
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

        std::vector<uint8_t> begin_payload_bytes;
        boltprotocol::PackStreamWriter writer(begin_payload_bytes);
        boltprotocol::BoltError err_code = boltprotocol::serialize_begin_message(params, writer, conn->get_bolt_version());
        if (err_code != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("BEGIN serialization", err_code);
            _invalidate_session_due_to_connection_error(err_code, msg);
            return {err_code, msg};
        }

        boltprotocol::SuccessMessageParams success_meta_raw;
        boltprotocol::FailureMessageParams failure_meta_raw;
        err_code = conn->send_request_receive_summary(begin_payload_bytes, success_meta_raw, failure_meta_raw);

        if (err_code == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
                in_explicit_transaction_ = true;
                current_transaction_query_id_.reset();
                if (logger) {
                    logger->info("[SessionTX {}] Transaction started. DB: '{}'", conn->get_id(), params.db.value_or("default"));
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string server_fail_msg = error::format_server_failure(failure_meta_raw);
                std::string msg = error::format_error_message("BEGIN failed on server", conn->get_last_error_code(), server_fail_msg);
                _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
                return {conn->get_last_error_code(), msg};
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

        std::vector<uint8_t> commit_payload_bytes;
        boltprotocol::PackStreamWriter writer(commit_payload_bytes);
        boltprotocol::BoltError err_code = boltprotocol::serialize_commit_message(writer);
        if (err_code != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("COMMIT serialization", err_code);
            _invalidate_session_due_to_connection_error(err_code, msg);
            in_explicit_transaction_ = false;
            current_transaction_query_id_.reset();
            return {err_code, msg};
        }

        boltprotocol::SuccessMessageParams success_meta_raw;
        boltprotocol::FailureMessageParams failure_meta_raw;
        err_code = conn->send_request_receive_summary(commit_payload_bytes, success_meta_raw, failure_meta_raw);

        in_explicit_transaction_ = false;
        current_transaction_query_id_.reset();

        if (err_code == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
                auto it_bookmark = success_meta_raw.metadata.find("bookmark");
                if (it_bookmark != success_meta_raw.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                    update_bookmarks({std::get<std::string>(it_bookmark->second)});
                } else {
                    update_bookmarks({});
                }
                if (logger) {
                    logger->info("[SessionTX {}] Transaction committed. New bookmark: {}", conn->get_id(), current_bookmarks_.empty() ? "<none>" : current_bookmarks_[0]);
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string msg = error::format_error_message("COMMIT failed on server", conn->get_last_error_code(), error::format_server_failure(failure_meta_raw));
                _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
                return {conn->get_last_error_code(), msg};
            }
        }
        std::string msg = error::format_error_message("COMMIT send/receive", err_code, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err_code, msg);
        return {err_code, msg};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::rollback_transaction() {
        std::pair<boltprotocol::BoltError, std::string> conn_check_result;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_result, "rollback_transaction (pre-check)");

        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (conn)
            logger = conn->get_logger();
        else if (transport_manager_)
            logger = transport_manager_->get_config().logger;

        if (!in_explicit_transaction_) {
            if (logger) logger->trace("[SessionTX {}] Rollback called when not in an explicit transaction. No-op.", (conn ? conn->get_id() : 0));
            return {boltprotocol::BoltError::SUCCESS, ""};
        }

        if (!conn) {
            std::string msg = "Rollback attempt with no valid connection while in TX: " + conn_check_result.second;
            if (logger) logger->warn("[SessionTX Managed] {}", msg);
            _invalidate_session_due_to_connection_error(conn_check_result.first, msg);
            in_explicit_transaction_ = false;
            current_transaction_query_id_.reset();
            return {conn_check_result.first, msg};
        }

        std::vector<uint8_t> rollback_payload_bytes;
        boltprotocol::PackStreamWriter writer(rollback_payload_bytes);
        boltprotocol::BoltError err_code = boltprotocol::serialize_rollback_message(writer);
        if (err_code != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("ROLLBACK serialization", err_code);
            _invalidate_session_due_to_connection_error(err_code, msg);
            in_explicit_transaction_ = false;
            current_transaction_query_id_.reset();
            return {err_code, msg};
        }

        boltprotocol::SuccessMessageParams success_meta_raw;
        boltprotocol::FailureMessageParams failure_meta_raw;
        err_code = conn->send_request_receive_summary(rollback_payload_bytes, success_meta_raw, failure_meta_raw);

        in_explicit_transaction_ = false;
        current_transaction_query_id_.reset();

        if (err_code == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
                if (logger) {
                    logger->info("[SessionTX {}] Transaction rolled back.", conn->get_id());
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string msg = error::format_error_message("ROLLBACK failed on server", conn->get_last_error_code(), error::format_server_failure(failure_meta_raw));
                _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
                return {conn->get_last_error_code(), msg};
            }
        }
        std::string msg = error::format_error_message("ROLLBACK send/receive", err_code, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err_code, msg);
        return {err_code, msg};
    }

    TransactionWorkResult SessionHandle::_execute_transaction_work_internal(TransactionWork work, config::AccessMode mode_hint, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        std::shared_ptr<spdlog::logger> drv_logger = nullptr;
        if (transport_manager_) drv_logger = transport_manager_->get_config().logger;

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

        // 保存原始的会话访问模式，以便在函数结束时恢复
        config::AccessMode original_session_access_mode = session_params_.default_access_mode;
        // 为本次事务设置访问模式提示
        session_params_.default_access_mode = mode_hint;

        while (std::chrono::steady_clock::now() < overall_deadline) {
            attempt_count++;
            std::shared_ptr<spdlog::logger> current_op_logger = drv_logger;
            if (connection_ && connection_->get_logger()) {
                current_op_logger = connection_->get_logger();
            }

            if (current_op_logger) {
                current_op_logger->debug("[SessionTX Managed] Attempt #{} for transaction work (Mode: {}).", attempt_count, (mode_hint == config::AccessMode::READ ? "READ" : "WRITE"));
            }

            auto begin_res = begin_transaction(tx_metadata, tx_timeout);
            if (begin_res.first != boltprotocol::BoltError::SUCCESS) {
                bool is_retryable_begin_failure = (begin_res.first == boltprotocol::BoltError::NETWORK_ERROR);  // 简化
                last_attempt_result = {begin_res.first, "Managed TX: Failed to begin (Attempt " + std::to_string(attempt_count) + "): " + begin_res.second};

                if (current_op_logger) current_op_logger->warn("[SessionTX Managed] {}", last_attempt_result.second);

                if (is_retryable_begin_failure && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (current_op_logger) {
                        current_op_logger->info("[SessionTX Managed] Retrying BEGIN in {}ms.", current_delay_ms);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                    // 如果 begin_transaction 失败是因为连接问题，它内部的 _invalidate_session_due_to_connection_error
                    // 会将 connection_is_valid_ 设为 false。如果连接池支持，下次 _get_valid_connection_for_operation
                    // 可能会尝试获取新连接。如果 _release_connection_to_pool 被调用，则连接会被标记为不健康。
                    // 对于托管事务，如果连接在 BEGIN 时就失效，我们可能需要一种机制来重新获取整个会话，
                    // 而不仅仅是重试当前会话上的 BEGIN。
                    // 当前简化：如果连接在 BEGIN 时失效，begin_transaction 会返回错误，
                    // is_retryable_begin_failure 会为 true，我们会 sleep 然后重试。
                    // 下一次 begin_transaction 内部的 _get_valid_connection_for_operation 仍然会失败，
                    // 除非更高层处理了 SessionHandle 的重新获取。
                    // 这是一个需要仔细考虑的设计点：托管事务的重试级别。
                    // 如果连接失效，我们应该在这里显式释放它。
                    if (!connection_is_valid_ && connection_) {
                        _release_connection_to_pool(false);  // 释放坏连接
                    }
                    continue;
                }
                session_params_.default_access_mode = original_session_access_mode;  // 恢复原始模式
                return last_attempt_result;
            }

            // BEGIN 成功，更新 current_op_logger 以防连接对象已更改（虽然不太可能在这里）
            if (connection_ && connection_->get_logger()) current_op_logger = connection_->get_logger();

            TransactionContext tx_context(*this);
            TransactionWorkResult work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "User work lambda not called."};
            bool user_work_threw = false;

            // 假设在 BEGIN 成功后，连接是有效的。
            // 如果在用户 lambda 执行期间连接失效，我们需要捕获这种情况。
            // connection_is_valid_ 应该在 SessionHandle 的操作中被更新。

            try {
                work_res = work(tx_context);  // 执行用户提供的事务逻辑
            } catch (const std::exception& e) {
                work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "Exception from user transaction work: " + std::string(e.what())};
                user_work_threw = true;
                if (current_op_logger) current_op_logger->error("[SessionTX Managed] {}", work_res.second);
            } catch (...) {
                work_res = {boltprotocol::BoltError::UNKNOWN_ERROR, "Unknown exception from user transaction work."};
                user_work_threw = true;
                if (current_op_logger) current_op_logger->error("[SessionTX Managed] {}", work_res.second);
            }

            // 检查在用户 lambda 执行后连接是否仍然有效
            if (!connection_is_valid_) {
                if (current_op_logger) {
                    current_op_logger->warn("[SessionTX Managed] Connection became invalid during user work lambda execution. Last connection error code: {}", connection_ ? static_cast<int>(connection_->get_last_error_code()) : -1);
                }
                // 覆盖 work_res，因为网络错误优先
                work_res = {boltprotocol::BoltError::NETWORK_ERROR, "Connection lost during transaction work execution."};
                // 尝试回滚（很可能失败，但尽力而为）
                rollback_transaction();
                last_attempt_result = work_res;

                // 网络错误是可重试的
                if (std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (current_op_logger) current_op_logger->info("[SessionTX Managed] Retrying entire transaction due to connection loss during work in {}ms.", current_delay_ms);
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                    if (connection_) _release_connection_to_pool(false);  // 释放失效连接
                    continue;                                             // 重试整个事务
                } else {
                    session_params_.default_access_mode = original_session_access_mode;  // 恢复
                    return last_attempt_result;                                          // 超时
                }
            }

            if (work_res.first == boltprotocol::BoltError::SUCCESS) {  // 用户 lambda 成功完成
                auto commit_res = commit_transaction();
                if (commit_res.first == boltprotocol::BoltError::SUCCESS) {
                    if (current_op_logger) {
                        current_op_logger->info("[SessionTX Managed] Transaction work committed successfully.");
                    }
                    session_params_.default_access_mode = original_session_access_mode;  // 恢复原始模式
                    return {boltprotocol::BoltError::SUCCESS, ""};
                } else {  // COMMIT 失败
                    last_attempt_result = {commit_res.first, "Managed TX: Commit failed after successful work (Attempt " + std::to_string(attempt_count) + "): " + commit_res.second};
                    if (current_op_logger) current_op_logger->warn("[SessionTX Managed] {}", last_attempt_result.second);

                    // 检查 COMMIT 失败是否可重试（例如，网络错误，领导者切换）
                    bool is_commit_retryable = (commit_res.first == boltprotocol::BoltError::NETWORK_ERROR || !connection_is_valid_);
                    // Neo4j Java 驱动会检查特定的服务器错误码如 "Neo.TransientError.Transaction.CouldNotCommit"
                    // if (conn && conn->get_last_neo4j_error_code() == "Neo.TransientError.Transaction.CouldNotCommit") is_commit_retryable = true;

                    if (is_commit_retryable && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                        if (current_op_logger) {
                            current_op_logger->info("[SessionTX Managed] COMMIT failed with retryable error, retrying whole TX in {}ms.", current_delay_ms);
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                        current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                        if (!connection_is_valid_ && connection_) _release_connection_to_pool(false);  // 释放失效连接
                        continue;                                                                      // 重试整个事务
                    }
                    session_params_.default_access_mode = original_session_access_mode;  // 恢复
                    return last_attempt_result;                                          // COMMIT 失败且不可重试或超时
                }
            } else {                                         // 用户 lambda 返回错误或抛出异常
                auto rollback_res = rollback_transaction();  // 尝试回滚
                if (rollback_res.first != boltprotocol::BoltError::SUCCESS && connection_is_valid_ && current_op_logger) {
                    // 记录回滚失败，但这通常不会改变用户 lambda 的原始错误结果
                    current_op_logger->warn("[SessionTX Managed] Rollback failed after work error ('{}'): {}", work_res.second, rollback_res.second);
                }
                last_attempt_result = work_res;  // 保留用户 lambda 的错误
                if (current_op_logger) current_op_logger->warn("[SessionTX Managed] Work failed (Attempt {}): {}", attempt_count, work_res.second);

                // 检查用户 lambda 返回的错误是否是可重试的
                // Neo4j Java 驱动会检查死锁 ("Neo.TransientError.Transaction.DeadlockDetected") 或其他瞬时错误
                bool is_work_error_retryable = (work_res.first == boltprotocol::BoltError::NETWORK_ERROR || !connection_is_valid_);
                // if (conn && conn->get_last_neo4j_error_code() == "Neo.TransientError.Transaction.DeadlockDetected") is_work_error_retryable = true;

                if (is_work_error_retryable && std::chrono::steady_clock::now() + std::chrono::milliseconds(current_delay_ms) < overall_deadline) {
                    if (current_op_logger) {
                        current_op_logger->info("[SessionTX Managed] Work failed with retryable error, retrying whole TX in {}ms.", current_delay_ms);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(current_delay_ms));
                    current_delay_ms = static_cast<uint32_t>(std::min(static_cast<double>(current_delay_ms) * multiplier, static_cast<double>(max_delay_ms)));
                    if (!connection_is_valid_ && connection_) _release_connection_to_pool(false);
                    continue;  // 重试整个事务
                }
                session_params_.default_access_mode = original_session_access_mode;  // 恢复
                return last_attempt_result;                                          // 工作错误且不可重试或超时
            }
        }  // end while retry loop

        // 如果循环结束仍未成功，则返回最后一次尝试的结果
        if (drv_logger) {
            drv_logger->warn("[SessionTX Managed] Transaction work failed after all {} retries or timeout. Last error: {}", attempt_count, last_attempt_result.second);
        }
        session_params_.default_access_mode = original_session_access_mode;  // 恢复原始模式
        return last_attempt_result;
    }

    TransactionWorkResult SessionHandle::execute_read_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        // config::AccessMode original_mode = session_params_.default_access_mode; // 已移到 _execute_transaction_work_internal
        // session_params_.default_access_mode = config::AccessMode::READ;
        TransactionWorkResult result = _execute_transaction_work_internal(std::move(work), config::AccessMode::READ, tx_metadata, tx_timeout);
        // session_params_.default_access_mode = original_mode;
        return result;
    }

    TransactionWorkResult SessionHandle::execute_write_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        // config::AccessMode original_mode = session_params_.default_access_mode; // 已移到 _execute_transaction_work_internal
        // session_params_.default_access_mode = config::AccessMode::WRITE;
        TransactionWorkResult result = _execute_transaction_work_internal(std::move(work), config::AccessMode::WRITE, tx_metadata, tx_timeout);
        // session_params_.default_access_mode = original_mode;
        return result;
    }

}  // namespace neo4j_bolt_transport