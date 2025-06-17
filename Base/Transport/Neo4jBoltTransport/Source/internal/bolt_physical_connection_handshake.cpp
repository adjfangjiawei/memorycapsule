// Source/internal/bolt_physical_connection_handshake.cpp
#include <iostream>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::_stage_bolt_handshake() {
            InternalState expected_prev_state;

            if (conn_config_.encryption_enabled) {
                expected_prev_state = InternalState::SSL_HANDSHAKEN;
                if (!ssl_stream_ || !ssl_stream_->lowest_layer().is_open()) {
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "SSL stream not ready for Bolt handshake.");
                    if (logger_) logger_->error("[ConnHandshake {}] SSL stream not ready for Bolt handshake. State: {}", id_, _get_current_state_as_string());
                    return last_error_code_;
                }
            } else {
                expected_prev_state = InternalState::TCP_CONNECTED;
                if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "Plain stream not ready for Bolt handshake.");
                    if (logger_) logger_->error("[ConnHandshake {}] Plain stream not ready for Bolt handshake. State: {}", id_, _get_current_state_as_string());
                    return last_error_code_;
                }
            }

            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s != expected_prev_state) {
                std::string msg = "Bolt handshake called in unexpected state: " + _get_current_state_as_string() + ". Expected: " + std::to_string(static_cast<int>(expected_prev_state));
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnHandshake {}] {}", id_, msg);
                return last_error_code_;
            }
            current_state_.store(InternalState::BOLT_HANDSHAKING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnHandshake {}] Performing Bolt handshake.", id_);

            std::vector<boltprotocol::versions::Version> proposed_versions = boltprotocol::versions::get_default_proposed_versions();
            if (proposed_versions.empty()) {
                _mark_as_defunct(boltprotocol::BoltError::INVALID_ARGUMENT, "No Bolt versions to propose for handshake.");
                if (logger_) logger_->error("[ConnHandshake {}] No Bolt versions to propose.", id_);
                return last_error_code_;
            }

            boltprotocol::BoltError err;
            if (conn_config_.encryption_enabled) {
                // 修正：只传递一个流对象，它既用于读也用于写
                err = boltprotocol::perform_handshake(*ssl_stream_, proposed_versions, negotiated_bolt_version_);
            } else {
                // 修正：只传递一个流对象
                err = boltprotocol::perform_handshake(*plain_iostream_wrapper_, proposed_versions, negotiated_bolt_version_);
            }

            if (err != boltprotocol::BoltError::SUCCESS) {
                std::string msg = "Bolt handshake failed: " + error::bolt_error_to_string(err);
                _mark_as_defunct(err, msg);
                if (logger_) logger_->error("[ConnHandshake {}] {}", id_, msg);
                return last_error_code_;
            }
            if (logger_) logger_->debug("[ConnHandshake {}] Bolt handshake successful. Negotiated version: {}.{}", id_, (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor);

            current_state_.store(InternalState::BOLT_HANDSHAKEN, std::memory_order_relaxed);
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            return boltprotocol::BoltError::SUCCESS;
        }

        // ... (函数 _stage_send_hello_and_initial_auth 保持不变) ...
        boltprotocol::BoltError BoltPhysicalConnection::_stage_send_hello_and_initial_auth() {
            if (current_state_.load(std::memory_order_relaxed) != InternalState::BOLT_HANDSHAKEN) {
                std::string msg = "HELLO/Auth stage called in unexpected state: " + _get_current_state_as_string();
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnHandshake {}] {}", id_, msg);
                return last_error_code_;
            }
            if (logger_) logger_->debug("[ConnHandshake {}] Sending HELLO and performing initial auth.", id_);

            boltprotocol::HelloMessageParams hello_p;
            hello_p.user_agent = conn_config_.user_agent_for_hello;
            hello_p.bolt_agent = conn_config_.bolt_agent_info_for_hello;

            bool auth_attempted_in_hello = false;
            if (negotiated_bolt_version_ < boltprotocol::versions::Version(5, 1)) {
                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, config::BasicAuth>) {
                            hello_p.auth_scheme = "basic";
                            hello_p.auth_principal = arg.username;
                            hello_p.auth_credentials = arg.password;
                            if (arg.realm.has_value()) {
                                hello_p.other_extra_tokens["realm"] = *arg.realm;
                            }
                            auth_attempted_in_hello = true;
                        } else if constexpr (std::is_same_v<T, config::CustomAuth>) {
                            hello_p.auth_scheme = arg.scheme;
                            hello_p.auth_principal = arg.principal;
                            hello_p.auth_credentials = arg.credentials;
                            if (arg.realm.has_value()) {
                                hello_p.other_extra_tokens["realm"] = *arg.realm;
                            }
                            if (arg.parameters.has_value()) {
                                hello_p.auth_scheme_specific_tokens = *arg.parameters;
                            }
                            auth_attempted_in_hello = true;
                        } else if constexpr (std::is_same_v<T, config::BearerAuth>) {
                            hello_p.auth_scheme = "bearer";
                            hello_p.auth_credentials = arg.token;
                            auth_attempted_in_hello = true;
                        } else if constexpr (std::is_same_v<T, config::KerberosAuth>) {
                            hello_p.auth_scheme = "kerberos";
                            hello_p.auth_credentials = arg.base64_ticket;
                            auth_attempted_in_hello = true;
                        } else if constexpr (std::is_same_v<T, config::NoAuth>) {
                            hello_p.auth_scheme = "none";
                            auth_attempted_in_hello = true;
                        }
                    },
                    conn_config_.auth_token);
            }

            if (conn_config_.hello_routing_context.has_value()) {
                hello_p.routing_context = *conn_config_.hello_routing_context;
            }

            if (negotiated_bolt_version_ == boltprotocol::versions::Version(4, 3) || negotiated_bolt_version_ == boltprotocol::versions::Version(4, 4)) {
                hello_p.patch_bolt = std::vector<std::string>{"utc"};
            }

            std::vector<uint8_t> hello_payload_bytes;
            boltprotocol::PackStreamWriter ps_writer(hello_payload_bytes);
            boltprotocol::BoltError err = boltprotocol::serialize_hello_message(hello_p, ps_writer, negotiated_bolt_version_);
            if (err != boltprotocol::BoltError::SUCCESS) {
                std::string msg = "HELLO serialization failed.";
                _mark_as_defunct(err, msg);
                if (logger_) logger_->error("[ConnHandshake {}] {}", id_, msg);
                return last_error_code_;
            }

            boltprotocol::SuccessMessageParams success_meta;
            boltprotocol::FailureMessageParams failure_meta;

            current_state_.store(InternalState::HELLO_AUTH_SENT, std::memory_order_relaxed);
            err = send_request_receive_summary(hello_payload_bytes, success_meta, failure_meta);

            if (err != boltprotocol::BoltError::SUCCESS) {
                return last_error_code_;
            }
            if (last_error_code_ != boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->warn("[ConnHandshake {}] HELLO failed on server. Code: {}, Msg: {}", id_, static_cast<int>(last_error_code_), last_error_message_);
                return last_error_code_;
            }
            _update_metadata_from_hello_success(success_meta);
            if (logger_) logger_->debug("[ConnHandshake {}] HELLO successful. Server: {}, ConnId: {}", id_, server_agent_string_, server_assigned_conn_id_);

            bool needs_separate_logon = false;
            if (!(negotiated_bolt_version_ < boltprotocol::versions::Version(5, 1)) && !auth_attempted_in_hello) {
                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (!std::is_same_v<T, config::NoAuth>) {
                            needs_separate_logon = true;
                        }
                    },
                    conn_config_.auth_token);
            }

            if (needs_separate_logon) {
                if (logger_) logger_->debug("[ConnHandshake {}] Bolt >= 5.1, performing separate LOGON.", id_);
                boltprotocol::LogonMessageParams logon_p;
                _prepare_logon_params_from_config(logon_p);

                boltprotocol::SuccessMessageParams logon_success_meta;
                err = _execute_logon_message(logon_p, logon_success_meta, failure_meta);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    if (logger_) logger_->error("[ConnHandshake {}] Separate LOGON failed. Code: {}, Msg: {}", id_, static_cast<int>(last_error_code_), last_error_message_);
                    return last_error_code_;
                }
                _update_metadata_from_logon_success(logon_success_meta);
                if (logger_) logger_->debug("[ConnHandshake {}] Separate LOGON successful.", id_);
            }

            if (current_state_.load(std::memory_order_relaxed) != InternalState::READY) {
                std::string msg = "Connection not READY after HELLO/LOGON sequence. State: " + _get_current_state_as_string();
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnHandshake {}] {}", id_, msg);
                return last_error_code_;
            }

            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport