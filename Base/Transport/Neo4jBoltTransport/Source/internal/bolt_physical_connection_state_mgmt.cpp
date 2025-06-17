#include <iostream>  // 调试用

#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // For error::bolt_error_to_string
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // _update_metadata_from_hello_success: 从 HELLO 成功消息中更新连接元数据
        void BoltPhysicalConnection::_update_metadata_from_hello_success(const boltprotocol::SuccessMessageParams& meta) {
            auto it_server = meta.metadata.find("server");
            if (it_server != meta.metadata.end() && std::holds_alternative<std::string>(it_server->second)) {
                server_agent_string_ = std::get<std::string>(it_server->second);
            } else {
                server_agent_string_.clear();  // 如果没有提供，则清空
            }

            auto it_conn_id = meta.metadata.find("connection_id");
            if (it_conn_id != meta.metadata.end() && std::holds_alternative<std::string>(it_conn_id->second)) {
                server_assigned_conn_id_ = std::get<std::string>(it_conn_id->second);
            } else {
                server_assigned_conn_id_.clear();
            }

            // 处理 UTC 补丁 (patch_bolt)
            utc_patch_active_ = false;  // 默认为 false
            // Bolt 4.3 和 4.4 可能通过 patch_bolt 启用 UTC
            if (negotiated_bolt_version_ == boltprotocol::versions::Version(4, 3) || negotiated_bolt_version_ == boltprotocol::versions::Version(4, 4)) {
                auto it_patch = meta.metadata.find("patch_bolt");
                if (it_patch != meta.metadata.end()) {
                    if (std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(it_patch->second)) {
                        auto list_ptr = std::get<std::shared_ptr<boltprotocol::BoltList>>(it_patch->second);
                        if (list_ptr) {
                            for (const auto& val : list_ptr->elements) {
                                if (std::holds_alternative<std::string>(val)) {
                                    if (std::get<std::string>(val) == "utc") {
                                        utc_patch_active_ = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // Bolt 5.0 及更高版本默认启用 UTC 行为（即时间戳为UTC）
            if (negotiated_bolt_version_ >= boltprotocol::versions::Version(5, 0)) {
                utc_patch_active_ = true;
            }

            if (logger_) logger_->debug("[ConnState {}] Metadata updated from HELLO. Server: '{}', ConnId: '{}', UTC Patch Active: {}", id_, server_agent_string_, server_assigned_conn_id_, utc_patch_active_);
        }

        // _update_metadata_from_logon_success: 从 LOGON 成功消息中更新元数据
        void BoltPhysicalConnection::_update_metadata_from_logon_success(const boltprotocol::SuccessMessageParams& meta) {
            // LOGON 响应也可能包含 "connection_id"，如果服务器选择在此时分配或更改它
            auto it_conn_id = meta.metadata.find("connection_id");
            if (it_conn_id != meta.metadata.end() && std::holds_alternative<std::string>(it_conn_id->second)) {
                std::string new_conn_id = std::get<std::string>(it_conn_id->second);
                if (server_assigned_conn_id_ != new_conn_id && logger_) {
                    logger_->debug("[ConnState {}] Connection ID changed by LOGON from '{}' to '{}'", id_, server_assigned_conn_id_, new_conn_id);
                }
                server_assigned_conn_id_ = new_conn_id;
            }
            // LOGON 响应通常不包含其他如 server agent 或 patch_bolt 的元数据，这些在 HELLO 中处理
            if (logger_) logger_->debug("[ConnState {}] Metadata updated from LOGON. ConnId: '{}'", id_, server_assigned_conn_id_);
        }

        // _classify_and_set_server_failure: 分类服务器 FAILURE 消息并设置连接错误状态
        boltprotocol::BoltError BoltPhysicalConnection::_classify_and_set_server_failure(const boltprotocol::FailureMessageParams& meta) {
            std::string neo4j_code = "Unknown.Error";
            std::string message = "An unspecified error occurred on the server.";

            auto extract_string_from_map = [&](const std::string& key) -> std::optional<std::string> {
                auto it = meta.metadata.find(key);
                if (it != meta.metadata.end() && std::holds_alternative<std::string>(it->second)) {
                    return std::get<std::string>(it->second);
                }
                return std::nullopt;
            };

            // Neo4j 错误码优先 (Bolt 5.1+)
            if (auto code_opt = extract_string_from_map("neo4j_code")) {
                neo4j_code = *code_opt;
            } else if (auto legacy_code_opt = extract_string_from_map("code")) {  // 旧版 "code"
                neo4j_code = *legacy_code_opt;
            }

            if (auto msg_opt = extract_string_from_map("message")) {
                message = *msg_opt;
            }

            std::string full_error_message = "Server error: [" + neo4j_code + "] " + message;

            // 根据错误码分类并设置状态
            // 这是一个简化的分类，实际驱动会更细致
            boltprotocol::BoltError classified_error_code = boltprotocol::BoltError::UNKNOWN_ERROR;
            InternalState next_state = InternalState::FAILED_SERVER_REPORTED;  // 默认服务器报告错误后可重置

            if (neo4j_code.find("TransientError") != std::string::npos || neo4j_code.find("DatabaseUnavailable") != std::string::npos || neo4j_code.find("NotALeader") != std::string::npos || neo4j_code.find("ForbiddenOnReadOnlyDatabase") != std::string::npos) {
                classified_error_code = boltprotocol::BoltError::NETWORK_ERROR;  // 可视为网络/集群问题
                // next_state 保持 FAILED_SERVER_REPORTED，允许重试/获取新路由
            } else if (neo4j_code.find("ClientError.Security") != std::string::npos) {
                classified_error_code = boltprotocol::BoltError::HANDSHAKE_FAILED;  // 认证/授权问题
                next_state = InternalState::DEFUNCT;                                // 安全问题通常使连接失效
            } else if (neo4j_code.find("ClientError.Statement") != std::string::npos) {
                classified_error_code = boltprotocol::BoltError::INVALID_ARGUMENT;  // 语句错误
                // next_state 保持 FAILED_SERVER_REPORTED，通常语句错误不使连接失效
            } else if (neo4j_code.find("ClientError.Transaction") != std::string::npos) {
                // 事务相关的客户端错误，例如无效的事务状态
                classified_error_code = boltprotocol::BoltError::UNKNOWN_ERROR;  // 或更具体的事务错误码
                // next_state 保持 FAILED_SERVER_REPORTED
            } else {
                // 其他未知错误
                // next_state 保持 FAILED_SERVER_REPORTED
            }

            // _mark_as_defunct 会将状态设为 DEFUNCT，这里我们根据错误类型决定是否调用它
            if (next_state == InternalState::DEFUNCT) {
                _mark_as_defunct(classified_error_code, full_error_message);
            } else {
                // 对于非 DEFUNCT 的服务器错误，我们设置错误码和消息，并将状态设为 FAILED_SERVER_REPORTED
                current_state_.store(InternalState::FAILED_SERVER_REPORTED, std::memory_order_relaxed);
                last_error_code_ = classified_error_code;
                last_error_message_ = full_error_message;
            }

            if (logger_) logger_->warn("[ConnState {}] Server reported failure. Code: '{}', Msg: '{}'. Classified as: {}, Next state: {}", id_, neo4j_code, message, error::bolt_error_to_string(last_error_code_), _get_current_state_as_string());
            return last_error_code_;  // 返回分类后的错误码
        }

        // _mark_as_defunct: 将连接标记为失效
        void BoltPhysicalConnection::_mark_as_defunct(boltprotocol::BoltError reason, const std::string& message) {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            // 如果已经失效且原因是相同的，则只追加消息（如果不同）
            if (current_s == InternalState::DEFUNCT && last_error_code_ != boltprotocol::BoltError::SUCCESS && reason == last_error_code_) {
                if (!message.empty() && last_error_message_.find(message) == std::string::npos) {
                    last_error_message_ += "; Additional info: " + message;
                }
                return;  // 避免重复日志和不必要的原子操作
            }

            current_state_.store(InternalState::DEFUNCT, std::memory_order_acq_rel);  // 使用 acquire/release 语义
            last_error_code_ = reason;
            last_error_message_ = message;

            if (logger_) {
                logger_->error("[ConnState {}] Marked as DEFUNCT. Reason: {} ({}), Message: {}", id_, static_cast<int>(reason), error::bolt_error_to_string(reason), message);
            }
        }

        // _get_current_state_as_string: 获取当前状态的字符串表示
        std::string BoltPhysicalConnection::_get_current_state_as_string() const {
            switch (current_state_.load(std::memory_order_relaxed)) {
                case InternalState::FRESH:
                    return "FRESH";
                case InternalState::TCP_CONNECTING:
                    return "TCP_CONNECTING";
                case InternalState::TCP_CONNECTED:
                    return "TCP_CONNECTED";
                case InternalState::SSL_CONTEXT_SETUP:
                    return "SSL_CONTEXT_SETUP";
                case InternalState::SSL_HANDSHAKING:
                    return "SSL_HANDSHAKING";
                case InternalState::SSL_HANDSHAKEN:
                    return "SSL_HANDSHAKEN";
                case InternalState::BOLT_HANDSHAKING:
                    return "BOLT_HANDSHAKING";
                case InternalState::BOLT_HANDSHAKEN:
                    return "BOLT_HANDSHAKEN";
                case InternalState::HELLO_AUTH_SENT:
                    return "HELLO_AUTH_SENT";
                case InternalState::READY:
                    return "READY";
                case InternalState::STREAMING:
                    return "STREAMING";
                case InternalState::AWAITING_SUMMARY:
                    return "AWAITING_SUMMARY";
                case InternalState::FAILED_SERVER_REPORTED:
                    return "FAILED_SERVER_REPORTED";
                case InternalState::DEFUNCT:
                    return "DEFUNCT";
                default:
                    return "UNKNOWN_STATE_" + std::to_string(static_cast<int>(current_state_.load(std::memory_order_relaxed)));
            }
        }

        // is_ready_for_queries: 检查连接是否准备好执行查询
        bool BoltPhysicalConnection::is_ready_for_queries() const {
            if (current_state_.load(std::memory_order_relaxed) != InternalState::READY) {
                return false;
            }
            // 检查底层流是否仍然有效
            if (conn_config_.encryption_enabled) {
                return ssl_stream_sync_ && ssl_stream_sync_->lowest_layer().is_open();  // 使用 _sync_ 后缀
            } else {
                return plain_iostream_wrapper_ && plain_iostream_wrapper_->good();
            }
        }

        // is_defunct: 检查连接是否已失效
        bool BoltPhysicalConnection::is_defunct() const {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s == InternalState::DEFUNCT) {
                return true;
            }
            // 即使状态不是 DEFUNCT，如果底层流已关闭，也应视为失效
            if (current_s > InternalState::FRESH && current_s < InternalState::DEFUNCT) {  // 意味着至少尝试过连接
                if (conn_config_.encryption_enabled) {
                    if (!ssl_stream_sync_ || !ssl_stream_sync_->lowest_layer().is_open()) {  // 使用 _sync_ 后缀
                        // (const_cast<BoltPhysicalConnection*>(this))->_mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "SSL stream found closed unexpectedly.");
                        return true;
                    }
                } else {
                    if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                        // (const_cast<BoltPhysicalConnection*>(this))->_mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "Plain stream found bad unexpectedly.");
                        return true;
                    }
                }
            }
            return false;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport