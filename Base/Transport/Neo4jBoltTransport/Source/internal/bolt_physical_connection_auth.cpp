#include <boost/asio/co_spawn.hpp>       // For co_spawn
#include <boost/asio/detached.hpp>       // For detached
#include <boost/asio/use_awaitable.hpp>  // For use_awaitable
#include <iostream>                      // 调试用

#include "boltprotocol/message_serialization.h"  // For serialize_logon_message
#include "boltprotocol/packstream_writer.h"      // For PackStreamWriter
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
                        out_params.auth_tokens["credentials"] = arg.base64_ticket;
                    } else if constexpr (std::is_same_v<T, config::CustomAuth>) {
                        out_params.auth_tokens["scheme"] = arg.scheme;
                        out_params.auth_tokens["principal"] = arg.principal;
                        out_params.auth_tokens["credentials"] = arg.credentials;
                        if (arg.realm) out_params.auth_tokens["realm"] = *arg.realm;
                        if (arg.parameters) {
                            for (const auto& pair : *arg.parameters) {
                                if (pair.first != "scheme" && pair.first != "principal" && pair.first != "credentials" && pair.first != "realm") {
                                    out_params.auth_tokens[pair.first] = pair.second;
                                }
                            }
                        }
                    } else if constexpr (std::is_same_v<T, config::NoAuth>) {
                        out_params.auth_tokens["scheme"] = std::string("none");
                    }
                },
                conn_config_.auth_token);
        }

        boltprotocol::BoltError BoltPhysicalConnection::_execute_logon_message(const boltprotocol::LogonMessageParams& params, boltprotocol::SuccessMessageParams& out_success, boltprotocol::FailureMessageParams& out_failure) {
            InternalState state_before_logon = current_state_.load();
            if (state_before_logon != InternalState::HELLO_AUTH_SENT && state_before_logon != InternalState::READY) {
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, "LOGON executed in invalid state: " + _get_current_state_as_string());
                if (logger_) logger_->error("[ConnAuth {}] LOGON in invalid state {}", id_, _get_current_state_as_string());
                return last_error_code_;
            }

            std::vector<uint8_t> logon_payload;
            boltprotocol::PackStreamWriter ps_writer(logon_payload);
            boltprotocol::BoltError err = boltprotocol::serialize_logon_message(params, ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct(err, "LOGON serialization failed.");
                if (logger_) logger_->error("[ConnAuth {}] LOGON serialization failed: {}", id_, static_cast<int>(err));
                return last_error_code_;
            }

            if (logger_) logger_->debug("[ConnAuth {}] Sending LOGON message (scheme: {}).", id_, params.auth_tokens.count("scheme") ? std::get<std::string>(params.auth_tokens.at("scheme")) : "unknown");

            err = send_request_receive_summary(logon_payload, out_success, out_failure);

            if (err == boltprotocol::BoltError::SUCCESS) {
                if (last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                    _update_metadata_from_logon_success(out_success);
                    if (logger_) logger_->info("[ConnAuth {}] LOGON successful.", id_);
                } else {
                    if (logger_) logger_->warn("[ConnAuth {}] LOGON server response not SUCCESS. Code: {}, Msg: {}", id_, static_cast<int>(last_error_code_), last_error_message_);
                }
            } else {
                if (logger_) logger_->error("[ConnAuth {}] LOGON message send/receive summary failed. Error: {}", id_, static_cast<int>(err));
            }
            return last_error_code_;
        }

        boltprotocol::BoltError BoltPhysicalConnection::perform_logon(const boltprotocol::LogonMessageParams& logon_params, boltprotocol::SuccessMessageParams& out_success) {
            if (negotiated_bolt_version_ < boltprotocol::versions::V5_1) {
                last_error_code_ = boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION;
                last_error_message_ = "LOGON message not supported in Bolt version < 5.1";
                if (logger_) logger_->warn("[ConnAuth {}] {}", id_, last_error_message_);
                return last_error_code_;
            }

            InternalState current_s = current_state_.load();
            if (current_s != InternalState::READY && current_s != InternalState::HELLO_AUTH_SENT) {
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, "perform_logon called in invalid state " + _get_current_state_as_string());
                if (logger_) logger_->warn("[ConnAuth {}] perform_logon in invalid state {}", id_, _get_current_state_as_string());
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
                if (logger_) logger_->warn("[ConnAuth {}] {}", id_, last_error_message_);
                return last_error_code_;
            }
            if (!is_ready_for_queries()) {
                std::string msg = "perform_logoff called when connection not ready. Current state: " + _get_current_state_as_string();
                if (logger_) logger_->warn("[ConnAuth {}] {}", id_, msg);
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            mark_as_used();
            if (logger_) logger_->debug("[ConnAuth {}] Performing LOGOFF.", id_);

            std::vector<uint8_t> logoff_payload;
            boltprotocol::PackStreamWriter ps_writer(logoff_payload);
            boltprotocol::BoltError err = boltprotocol::serialize_logoff_message(ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct(err, "LOGOFF serialization failed.");
                if (logger_) logger_->error("[ConnAuth {}] LOGOFF serialization failed: {}", id_, static_cast<int>(err));
                return last_error_code_;
            }

            boltprotocol::FailureMessageParams ignored_failure_details;
            err = send_request_receive_summary(logoff_payload, out_success, ignored_failure_details);

            if (err == boltprotocol::BoltError::SUCCESS && last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->info("[ConnAuth {}] LOGOFF successful.", id_);
                current_state_.store(InternalState::HELLO_AUTH_SENT, std::memory_order_relaxed);
            } else {
                if (logger_) logger_->warn("[ConnAuth {}] LOGOFF failed. Error: {}, Server Msg: {}", id_, static_cast<int>(err), last_error_message_);
            }
            return last_error_code_;
        }

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> BoltPhysicalConnection::_execute_logon_message_async(boltprotocol::LogonMessageParams params,
                                                                                                                                                            boost::asio::ip::tcp::socket* /*plain_socket_unused*/,
                                                                                                                                                            boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>* /*ssl_socket_stream_unused*/) {
            if (logger_) logger_->debug("[ConnAuthAsync {}] Executing LOGON message async (scheme: {}).", id_, params.auth_tokens.count("scheme") ? std::get<std::string>(params.auth_tokens.at("scheme")) : "unknown");

            std::vector<uint8_t> logon_payload_storage;
            boltprotocol::PackStreamWriter ps_writer(logon_payload_storage);

            boltprotocol::BoltError err = boltprotocol::serialize_logon_message(params, ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct(err, "Async LOGON serialization failed.");
                if (logger_) logger_->error("[ConnAuthAsync {}] LOGON serialization failed: {}", id_, static_cast<int>(err));
                co_return std::make_pair(last_error_code_, boltprotocol::SuccessMessageParams{});
            }

            auto [summary_err, success_meta] = co_await send_request_receive_summary_async(std::move(logon_payload_storage));

            if (summary_err == boltprotocol::BoltError::SUCCESS) {
                if (last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                    _update_metadata_from_logon_success(success_meta);
                    if (logger_) logger_->info("[ConnAuthAsync {}] Async LOGON successful.", id_);
                    co_return std::make_pair(boltprotocol::BoltError::SUCCESS, success_meta);
                } else {
                    if (logger_) logger_->warn("[ConnAuthAsync {}] Async LOGON server response not SUCCESS. Code: {}, Msg: {}", id_, static_cast<int>(last_error_code_), last_error_message_);
                    co_return std::make_pair(last_error_code_, boltprotocol::SuccessMessageParams{});
                }
            } else {
                if (logger_) logger_->error("[ConnAuthAsync {}] Async LOGON send/receive summary failed. Error: {}", id_, static_cast<int>(summary_err));
                co_return std::make_pair(summary_err, boltprotocol::SuccessMessageParams{});
            }
        }

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> BoltPhysicalConnection::perform_logon_async(boltprotocol::LogonMessageParams logon_params) {
            if (negotiated_bolt_version_ < boltprotocol::versions::V5_1) {
                if (logger_) logger_->warn("[ConnAuthAsync {}] perform_logon_async: LOGON not supported in Bolt < 5.1", id_);
                co_return std::make_pair(boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION, boltprotocol::SuccessMessageParams{});
            }
            InternalState current_s = current_state_.load();
            if (current_s != InternalState::READY && current_s != InternalState::HELLO_AUTH_SENT) {
                if (logger_) logger_->warn("[ConnAuthAsync {}] perform_logon_async in invalid state {}", id_, _get_current_state_as_string());
                co_return std::make_pair(boltprotocol::BoltError::UNKNOWN_ERROR, boltprotocol::SuccessMessageParams{});
            }
            mark_as_used();
            co_return co_await _execute_logon_message_async(std::move(logon_params), nullptr, nullptr);
        }

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> BoltPhysicalConnection::perform_logoff_async() {
            if (negotiated_bolt_version_ < boltprotocol::versions::V5_1) {
                if (logger_) logger_->warn("[ConnAuthAsync {}] perform_logoff_async: LOGOFF not supported in Bolt < 5.1", id_);
                co_return std::make_pair(boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION, boltprotocol::SuccessMessageParams{});
            }
            if (!is_ready_for_queries()) {
                if (logger_) logger_->warn("[ConnAuthAsync {}] perform_logoff_async called when not ready. State: {}", id_, _get_current_state_as_string());
                co_return std::make_pair(last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR, boltprotocol::SuccessMessageParams{});
            }
            mark_as_used();
            if (logger_) logger_->debug("[ConnAuthAsync {}] Performing LOGOFF async.", id_);

            std::vector<uint8_t> logoff_payload_storage;
            boltprotocol::PackStreamWriter ps_writer(logoff_payload_storage);
            boltprotocol::BoltError err = boltprotocol::serialize_logoff_message(ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct(err, "Async LOGOFF serialization failed.");
                if (logger_) logger_->error("[ConnAuthAsync {}] LOGOFF serialization failed: {}", id_, static_cast<int>(err));
                co_return std::make_pair(last_error_code_, boltprotocol::SuccessMessageParams{});
            }

            auto [summary_err, success_meta] = co_await send_request_receive_summary_async(std::move(logoff_payload_storage));

            if (summary_err == boltprotocol::BoltError::SUCCESS && last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->info("[ConnAuthAsync {}] Async LOGOFF successful.", id_);
                current_state_.store(InternalState::HELLO_AUTH_SENT, std::memory_order_relaxed);
                co_return std::make_pair(boltprotocol::BoltError::SUCCESS, success_meta);
            } else {
                boltprotocol::BoltError final_err = last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : summary_err;
                if (final_err == boltprotocol::BoltError::SUCCESS && summary_err != boltprotocol::BoltError::SUCCESS) {  // Should not happen if last_error_code was success
                    final_err = summary_err;
                } else if (final_err == boltprotocol::BoltError::SUCCESS && summary_err == boltprotocol::BoltError::SUCCESS && last_error_code_ != boltprotocol::BoltError::SUCCESS) {
                    // This case implies server sent SUCCESS for LOGOFF but we had a prior different error for the exchange. Unlikely.
                }

                if (logger_) logger_->warn("[ConnAuthAsync {}] Async LOGOFF failed. SummaryExchangeError: {}, FinalConnError: {}, Server Msg: {}", id_, static_cast<int>(summary_err), static_cast<int>(last_error_code_), last_error_message_);
                co_return std::make_pair(final_err, boltprotocol::SuccessMessageParams{});
            }
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport