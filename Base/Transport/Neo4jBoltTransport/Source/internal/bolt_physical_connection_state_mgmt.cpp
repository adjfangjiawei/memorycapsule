// Source/internal/bolt_physical_connection_state_mgmt.cpp
#include <iostream>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // For error::bolt_error_to_string
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

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
            // Use Version constructor for comparison
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
            if (!(negotiated_bolt_version_ < boltprotocol::versions::Version(5, 0))) {
                utc_patch_active_ = true;
            }
            if (logger_) logger_->debug("[ConnState {}] Metadata updated from HELLO. Server: {}, ConnId: {}, UTC Patch: {}", id_, server_agent_string_, server_assigned_conn_id_, utc_patch_active_);
        }

        void BoltPhysicalConnection::_update_metadata_from_logon_success(const boltprotocol::SuccessMessageParams& meta) {
            auto it_conn_id = meta.metadata.find("connection_id");
            if (it_conn_id != meta.metadata.end() && std::holds_alternative<std::string>(it_conn_id->second)) {
                if (server_assigned_conn_id_ != std::get<std::string>(it_conn_id->second) && logger_) {
                    logger_->debug("[ConnState {}] Connection ID changed by LOGON from {} to {}", id_, server_assigned_conn_id_, std::get<std::string>(it_conn_id->second));
                }
                server_assigned_conn_id_ = std::get<std::string>(it_conn_id->second);
            }
        }

        boltprotocol::BoltError BoltPhysicalConnection::_classify_and_set_server_failure(const boltprotocol::FailureMessageParams& meta) {
            std::string code = "Unknown.Error";
            std::string message = "An unspecified error occurred on the server.";

            auto extract_string = [&](const std::string& key) -> std::optional<std::string> {
                auto it = meta.metadata.find(key);
                if (it != meta.metadata.end() && std::holds_alternative<std::string>(it->second)) {
                    return std::get<std::string>(it->second);
                }
                return std::nullopt;
            };

            if (auto neo4j_code_opt = extract_string("neo4j_code")) {
                code = *neo4j_code_opt;
            } else if (auto legacy_code_opt = extract_string("code")) {
                code = *legacy_code_opt;
            }

            if (auto msg_opt = extract_string("message")) {
                message = *msg_opt;
            }

            last_error_message_ = "Server error: [" + code + "] " + message;

            if (code.find("TransientError") != std::string::npos || code.find("DatabaseUnavailable") != std::string::npos || code.find("NotALeader") != std::string::npos || code.find("ForbiddenOnReadOnlyDatabase") != std::string::npos) {
                current_state_.store(InternalState::FAILED_SERVER_REPORTED);
                last_error_code_ = boltprotocol::BoltError::NETWORK_ERROR;
            } else if (code.find("ClientError.Security") != std::string::npos) {
                current_state_.store(InternalState::DEFUNCT);
                last_error_code_ = boltprotocol::BoltError::HANDSHAKE_FAILED;
            } else if (code.find("ClientError.Statement") != std::string::npos) {
                current_state_.store(InternalState::FAILED_SERVER_REPORTED);
                last_error_code_ = boltprotocol::BoltError::INVALID_ARGUMENT;
            } else if (code.find("ClientError.Transaction") != std::string::npos) {
                current_state_.store(InternalState::FAILED_SERVER_REPORTED);
                last_error_code_ = boltprotocol::BoltError::UNKNOWN_ERROR;
            } else {
                current_state_.store(InternalState::FAILED_SERVER_REPORTED);
                last_error_code_ = boltprotocol::BoltError::UNKNOWN_ERROR;
            }
            if (logger_) logger_->warn("[ConnState {}] Server reported failure. Code: {}, Msg: {}, Classified as: {}", id_, code, message, static_cast<int>(last_error_code_));
            return last_error_code_;
        }

        void BoltPhysicalConnection::_mark_as_defunct(boltprotocol::BoltError reason, const std::string& message) {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s == InternalState::DEFUNCT && last_error_code_ != boltprotocol::BoltError::SUCCESS && reason == last_error_code_) {
                if (!message.empty() && last_error_message_.find(message) == std::string::npos) {
                    last_error_message_ += "; " + message;
                }
                return;
            }

            current_state_.store(InternalState::DEFUNCT, std::memory_order_acq_rel);
            last_error_code_ = reason;
            last_error_message_ = message;

            if (logger_) {
                // Use error::bolt_error_to_string
                logger_->error("[ConnState {}] Marked as DEFUNCT. Reason: {} ({}), Message: {}", id_, static_cast<int>(reason), error::bolt_error_to_string(reason), message);
            }
        }

        // ... (_get_current_state_as_string, is_ready_for_queries, is_defunct as previously corrected) ...
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

        bool BoltPhysicalConnection::is_ready_for_queries() const {
            if (current_state_.load(std::memory_order_relaxed) != InternalState::READY) {
                return false;
            }
            if (conn_config_.encryption_enabled) {
                return ssl_stream_ && ssl_stream_->lowest_layer().is_open();
            } else {
                return plain_iostream_wrapper_ && plain_iostream_wrapper_->good();
            }
        }

        bool BoltPhysicalConnection::is_defunct() const {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s == InternalState::DEFUNCT) {
                return true;
            }
            if (current_s > InternalState::FRESH && current_s < InternalState::DEFUNCT) {
                if (conn_config_.encryption_enabled) {
                    if (!ssl_stream_ || !ssl_stream_->lowest_layer().is_open()) {
                        return true;
                    }
                } else {
                    if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                        return true;
                    }
                }
            }
            return false;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport