#include <iostream>  // 调试用

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // _update_metadata_from_hello_success 和 _update_metadata_from_logon_success 保持不变

        void BoltPhysicalConnection::_update_metadata_from_hello_success(const boltprotocol::SuccessMessageParams& meta) {
            auto it_server = meta.metadata.find("server");
            if (it_server != meta.metadata.end() && std::holds_alternative<std::string>(it_server->second)) {
                server_agent_string_ = std::get<std::string>(it_server->second);
            } else {
                server_agent_string_.clear();
            }

            auto it_conn_id = meta.metadata.find("connection_id");
            if (it_conn_id != meta.metadata.end() && std::holds_alternative<std::string>(it_conn_id->second)) {
                server_assigned_conn_id_ = std::get<std::string>(it_conn_id->second);
            } else {
                server_assigned_conn_id_.clear();
            }

            utc_patch_active_ = false;
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
            if (negotiated_bolt_version_ >= boltprotocol::versions::Version(5, 0)) {
                utc_patch_active_ = true;
            }

            if (logger_) logger_->debug("[ConnState {}] Metadata updated from HELLO. Server: '{}', ConnId: '{}', UTC Patch Active: {}", id_, server_agent_string_, server_assigned_conn_id_, utc_patch_active_);
        }

        void BoltPhysicalConnection::_update_metadata_from_logon_success(const boltprotocol::SuccessMessageParams& meta) {
            auto it_conn_id = meta.metadata.find("connection_id");
            if (it_conn_id != meta.metadata.end() && std::holds_alternative<std::string>(it_conn_id->second)) {
                std::string new_conn_id = std::get<std::string>(it_conn_id->second);
                if (server_assigned_conn_id_ != new_conn_id && logger_) {
                    logger_->debug("[ConnState {}] Connection ID changed by LOGON from '{}' to '{}'", id_, server_assigned_conn_id_, new_conn_id);
                }
                server_assigned_conn_id_ = new_conn_id;
            }
            if (logger_) logger_->debug("[ConnState {}] Metadata updated from LOGON. ConnId: '{}'", id_, server_assigned_conn_id_);
        }

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

            if (auto code_opt = extract_string_from_map("neo4j_code")) {
                neo4j_code = *code_opt;
            } else if (auto legacy_code_opt = extract_string_from_map("code")) {
                neo4j_code = *legacy_code_opt;
            }

            if (auto msg_opt = extract_string_from_map("message")) {
                message = *msg_opt;
            }

            std::string full_error_message = "Server error: [" + neo4j_code + "] " + message;
            // 默认使用 UNKNOWN_ERROR 或 NETWORK_ERROR，因为 SERVER_FAILURE 等可能未定义
            boltprotocol::BoltError classified_error_code = boltprotocol::BoltError::UNKNOWN_ERROR;
            InternalState next_state = InternalState::FAILED_SERVER_REPORTED;

            // 映射到已有的错误码
            if (neo4j_code.find("TransientError") != std::string::npos || neo4j_code.find("DatabaseUnavailable") != std::string::npos || neo4j_code.find("NotALeader") != std::string::npos || neo4j_code.find("ForbiddenOnReadOnlyDatabase") != std::string::npos) {
                classified_error_code = boltprotocol::BoltError::NETWORK_ERROR;  // 可重试的网络/服务问题
            } else if (neo4j_code.find("ClientError.Security") != std::string::npos) {
                classified_error_code = boltprotocol::BoltError::HANDSHAKE_FAILED;  // 安全相关问题，可视为握手/认证失败
                next_state = InternalState::DEFUNCT;
            } else if (neo4j_code.find("ClientError.Statement") != std::string::npos) {
                classified_error_code = boltprotocol::BoltError::INVALID_ARGUMENT;  // 语句错误通常是参数问题
            } else if (neo4j_code.find("ClientError.Transaction") != std::string::npos) {
                // 对于事务错误，没有非常匹配的现有通用 BoltError，使用 UNKNOWN_ERROR
                classified_error_code = boltprotocol::BoltError::UNKNOWN_ERROR;
            }
            // 其他错误保持为 UNKNOWN_ERROR

            if (next_state == InternalState::DEFUNCT) {
                _mark_as_defunct_internal(classified_error_code, full_error_message);
            } else {
                current_state_.store(InternalState::FAILED_SERVER_REPORTED, std::memory_order_relaxed);
                last_error_code_ = classified_error_code;
                last_error_message_ = full_error_message;
            }

            if (logger_) logger_->warn("[ConnState {}] Server reported failure. Code: '{}', Msg: '{}'. Classified as: {}, Next state: {}", id_, neo4j_code, message, error::bolt_error_to_string(last_error_code_), _get_current_state_as_string());
            return last_error_code_;
        }

        // _mark_as_defunct_internal, mark_as_defunct_from_async, _get_current_state_as_string,
        // is_ready_for_queries, is_defunct 保持上次修复后的状态。

        void BoltPhysicalConnection::_mark_as_defunct_internal(boltprotocol::BoltError reason, const std::string& message) {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s == InternalState::DEFUNCT && last_error_code_ != boltprotocol::BoltError::SUCCESS && reason == last_error_code_) {
                if (!message.empty() && last_error_message_.find(message) == std::string::npos) {
                    last_error_message_ += "; Additional info: " + message;
                }
                if (message == last_error_message_ && reason == last_error_code_ && logger_) {
                    logger_->trace("[ConnState {}] _mark_as_defunct_internal called again with same reason and message for already defunct connection.", id_);
                }
                return;
            }
            current_state_.store(InternalState::DEFUNCT, std::memory_order_acq_rel);
            last_error_code_ = reason;
            last_error_message_ = message;
            if (logger_) {
                logger_->error("[ConnState {}] Marked as DEFUNCT. Reason: {} ({}), Message: {}", id_, static_cast<int>(reason), error::bolt_error_to_string(reason), message);
            }
        }

        void BoltPhysicalConnection::mark_as_defunct_from_async(boltprotocol::BoltError reason, const std::string& message) {
            _mark_as_defunct_internal(reason, message);
        }

        std::string BoltPhysicalConnection::_get_current_state_as_string() const {
            switch (current_state_.load(std::memory_order_relaxed)) {
                case InternalState::FRESH:
                    return "FRESH";
                case InternalState::TCP_CONNECTING:
                    return "TCP_CONNECTING";
                case InternalState::ASYNC_TCP_CONNECTING:
                    return "ASYNC_TCP_CONNECTING";
                case InternalState::TCP_CONNECTED:
                    return "TCP_CONNECTED";
                case InternalState::SSL_CONTEXT_SETUP:
                    return "SSL_CONTEXT_SETUP";
                case InternalState::SSL_HANDSHAKING:
                    return "SSL_HANDSHAKING";
                case InternalState::ASYNC_SSL_HANDSHAKING:
                    return "ASYNC_SSL_HANDSHAKING";
                case InternalState::SSL_HANDSHAKEN:
                    return "SSL_HANDSHAKEN";
                case InternalState::BOLT_HANDSHAKING:
                    return "BOLT_HANDSHAKING";
                case InternalState::ASYNC_BOLT_HANDSHAKING:
                    return "ASYNC_BOLT_HANDSHAKING";
                case InternalState::BOLT_HANDSHAKEN:
                    return "BOLT_HANDSHAKEN";
                case InternalState::ASYNC_BOLT_HANDSHAKEN:
                    return "ASYNC_BOLT_HANDSHAKEN";
                case InternalState::HELLO_AUTH_SENT:
                    return "HELLO_AUTH_SENT";
                case InternalState::ASYNC_HELLO_AUTH_SENT:
                    return "ASYNC_HELLO_AUTH_SENT";
                case InternalState::READY:
                    return "READY";
                case InternalState::ASYNC_READY:
                    return "ASYNC_READY";
                case InternalState::STREAMING:
                    return "STREAMING";
                case InternalState::ASYNC_STREAMING:
                    return "ASYNC_STREAMING";
                case InternalState::AWAITING_SUMMARY:
                    return "AWAITING_SUMMARY";
                case InternalState::ASYNC_AWAITING_SUMMARY:
                    return "ASYNC_AWAITING_SUMMARY";
                case InternalState::FAILED_SERVER_REPORTED:
                    return "FAILED_SERVER_REPORTED";
                case InternalState::DEFUNCT:
                    return "DEFUNCT";
                default:
                    return "UNKNOWN_STATE_" + std::to_string(static_cast<int>(current_state_.load(std::memory_order_relaxed)));
            }
        }

        bool BoltPhysicalConnection::is_ready_for_queries() const {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s != InternalState::READY && current_s != InternalState::ASYNC_READY) {
                return false;
            }
            if (current_s == InternalState::READY) {
                if (conn_config_.encryption_enabled) {
                    return ssl_stream_sync_ && ssl_stream_sync_->lowest_layer().is_open();
                } else {
                    return plain_iostream_wrapper_ && plain_iostream_wrapper_->good();
                }
            }
            if (current_s == InternalState::ASYNC_READY) {
                return true;
            }
            return false;
        }

        bool BoltPhysicalConnection::is_defunct() const {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s == InternalState::DEFUNCT) {
                return true;
            }
            if (current_s > InternalState::FRESH && current_s < InternalState::DEFUNCT) {
                if (current_s != InternalState::ASYNC_TCP_CONNECTING && current_s != InternalState::ASYNC_SSL_HANDSHAKING && current_s != InternalState::ASYNC_BOLT_HANDSHAKING && current_s != InternalState::ASYNC_HELLO_AUTH_SENT && current_s != InternalState::ASYNC_READY &&
                    current_s != InternalState::ASYNC_STREAMING && current_s != InternalState::ASYNC_AWAITING_SUMMARY) {
                    if (conn_config_.encryption_enabled) {
                        if (!ssl_stream_sync_ || !ssl_stream_sync_->lowest_layer().is_open()) {
                            return true;
                        }
                    } else {
                        if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport