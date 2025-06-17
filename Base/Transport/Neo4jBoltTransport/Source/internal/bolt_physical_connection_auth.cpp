#include <iostream>

#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        void BoltPhysicalConnection::_prepare_logon_params_from_config(boltprotocol::LogonMessageParams& out_params) const {
            out_params.auth_tokens.clear();
            std::visit(
                [&](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, config::BasicAuth>) {
                        out_params.auth_tokens["scheme"] = std::string("basic");
                        out_params.auth_tokens["principal"] = arg.username;
                        out_params.auth_tokens["credentials"] = arg.password;
                        if (arg.realm) out_params.auth_tokens["realm"] = *arg.realm;
                    } else if constexpr (std::is_same_v<T, config::BearerAuth>) {
                        out_params.auth_tokens["scheme"] = std::string("bearer");
                        out_params.auth_tokens["credentials"] = arg.token;
                    } else if constexpr (std::is_same_v<T, config::KerberosAuth>) {
                        out_params.auth_tokens["scheme"] = std::string("kerberos");
                        out_params.auth_tokens["credentials"] = arg.base64_ticket;  // The ticket itself
                    } else if constexpr (std::is_same_v<T, config::CustomAuth>) {
                        out_params.auth_tokens["scheme"] = arg.scheme;
                        out_params.auth_tokens["principal"] = arg.principal;
                        out_params.auth_tokens["credentials"] = arg.credentials;
                        if (arg.realm) out_params.auth_tokens["realm"] = *arg.realm;
                        if (arg.parameters) {
                            for (const auto& pair : *arg.parameters) {
                                // Ensure keys are not overriding standard ones unless intended by custom scheme
                                if (pair.first != "scheme" && pair.first != "principal" && pair.first != "credentials" && pair.first != "realm") {
                                    out_params.auth_tokens[pair.first] = pair.second;
                                }
                            }
                        }
                    }
                    // NoAuth does not result in LOGON typically, but if called, send empty tokens or specific "none" scheme.
                    // For robust NoAuth, LOGON might not even be sent.
                    // This function is primarily for schemes that DO use LOGON.
                },
                conn_config_.auth_token);
        }

        // This is the internal method that actually sends LOGON and handles response.
        // It's called by _stage_send_hello_and_initial_auth or by public perform_logon.
        boltprotocol::BoltError BoltPhysicalConnection::_execute_logon_message(const boltprotocol::LogonMessageParams& params, boltprotocol::SuccessMessageParams& out_success, boltprotocol::FailureMessageParams& out_failure) {
            // State check: Should be after HELLO response if part of initial sequence, or READY if re-authenticating.
            InternalState state_before_logon = current_state_.load();
            if (state_before_logon != InternalState::HELLO_AUTH_SENT && state_before_logon != InternalState::READY) {
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, "LOGON executed in invalid state: " + _get_current_state_as_string());
                return last_error_code_;
            }
            // current_state_ = InternalState::LOGON_SENT; // Or more specific if needed

            std::vector<uint8_t> logon_payload;
            boltprotocol::PackStreamWriter ps_writer(logon_payload);
            boltprotocol::BoltError err = boltprotocol::serialize_logon_message(params, ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct(err, "LOGON serialization failed.");
                return last_error_code_;
            }

            // LOGON expects a single summary response
            err = send_request_receive_summary(logon_payload, out_success, out_failure);
            // send_request_receive_summary updates current_state_ to READY on SUCCESS,
            // or calls _classify_and_set_server_failure on FAILURE/IGNORED.

            if (err == boltprotocol::BoltError::SUCCESS) {                   // Message exchange successful (got a summary)
                if (last_error_code_ == boltprotocol::BoltError::SUCCESS) {  // Server sent SUCCESS
                    _update_metadata_from_logon_success(out_success);
                    // State should be READY (set by send_request_receive_summary)
                }
                // If server sent FAILURE, last_error_code_ is already set by _classify...
            }
            // If send_request_receive_summary itself failed (IO error), it marked DEFUNCT
            return last_error_code_;  // Return the ultimate outcome
        }

        // Public methods for direct LOGON/LOGOFF (might be called by Session or advanced user)
        boltprotocol::BoltError BoltPhysicalConnection::perform_logon(const boltprotocol::LogonMessageParams& logon_params, boltprotocol::SuccessMessageParams& out_success) {
            if (negotiated_bolt_version_ < boltprotocol::versions::V5_1) {
                last_error_code_ = boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION;
                last_error_message_ = "LOGON message not supported in Bolt version < 5.1";
                return last_error_code_;
            }
            // Check if connection is in a state that allows LOGON (e.g., after HELLO if not auto-authed, or READY for re-auth)
            InternalState current_s = current_state_.load();
            if (current_s != InternalState::READY && current_s != InternalState::HELLO_AUTH_SENT) {
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, "perform_logon called in invalid state " + _get_current_state_as_string());
                return last_error_code_;
            }
            mark_as_used();

            boltprotocol::FailureMessageParams ignored_failure_details;
            return _execute_logon_message(logon_params, out_success, ignored_failure_details);
        }

        boltprotocol::BoltError BoltPhysicalConnection::perform_logoff(boltprotocol::SuccessMessageParams& out_success) {
            if (negotiated_bolt_version_ < boltprotocol::versions::V5_1) {
                last_error_code_ = boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION;
                last_error_message_ = "LOGOFF message not supported in Bolt version < 5.1";
                return last_error_code_;
            }
            if (!is_ready_for_queries()) {  // LOGOFF implies an active, authenticated session on this connection
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            mark_as_used();

            std::vector<uint8_t> logoff_payload;
            boltprotocol::PackStreamWriter ps_writer(logoff_payload);
            boltprotocol::BoltError err = boltprotocol::serialize_logoff_message(ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct(err, "LOGOFF serialization failed.");
                return last_error_code_;
            }

            boltprotocol::FailureMessageParams ignored_failure_details;
            err = send_request_receive_summary(logoff_payload, out_success, ignored_failure_details);

            if (err == boltprotocol::BoltError::SUCCESS && last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                // After successful LOGOFF, connection state is effectively unauthenticated from server's PoV.
                // Driver might need to re-HELLO or re-LOGON for further operations.
                // Setting state to HELLO_AUTH_SENT might be appropriate to indicate it needs auth again.
                // Or a new state like UNAUTHENTICATED. For now, keep it READY but user must be aware.
                // current_state_.store(InternalState::HELLO_AUTH_SENT); // Or a new UNAUTHENTICATED state
            }
            return last_error_code_;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport