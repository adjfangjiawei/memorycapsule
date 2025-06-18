#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <iostream>
#include <utility>  // For std::make_pair
#include <variant>

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"  // For PackStreamReader in perform_logoff_async
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // _prepare_logon_params_from_config (sync) - (保持不变)
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
                            for (const auto& pair_ : *arg.parameters) {
                                if (pair_.first != "scheme" && pair_.first != "principal" && pair_.first != "credentials" && pair_.first != "realm") {
                                    out_params.auth_tokens[pair_.first] = pair_.second;
                                }
                            }
                        }
                    } else if constexpr (std::is_same_v<T, config::NoAuth>) {
                        out_params.auth_tokens["scheme"] = std::string("none");
                    }
                },
                conn_config_.auth_token);
        }

        // _execute_logon_message (sync) - (保持不变)
        boltprotocol::BoltError BoltPhysicalConnection::_execute_logon_message(const boltprotocol::LogonMessageParams& params, boltprotocol::SuccessMessageParams& out_success, boltprotocol::FailureMessageParams& out_failure) {
            InternalState state_before_logon = current_state_.load();
            if (state_before_logon != InternalState::HELLO_AUTH_SENT && state_before_logon != InternalState::READY && state_before_logon != InternalState::BOLT_HANDSHAKEN) {
                _mark_as_defunct_internal(boltprotocol::BoltError::UNKNOWN_ERROR, "LOGON executed in invalid state: " + _get_current_state_as_string());
                if (logger_) logger_->error("[ConnAuth {}] LOGON in invalid state {}", id_, _get_current_state_as_string());
                return last_error_code_;
            }

            std::vector<uint8_t> logon_payload;
            boltprotocol::PackStreamWriter ps_writer(logon_payload);
            boltprotocol::BoltError err = boltprotocol::serialize_logon_message(params, ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct_internal(err, "LOGON serialization failed.");
                if (logger_) logger_->error("[ConnAuth {}] LOGON serialization failed: {}", id_, static_cast<int>(err));
                return last_error_code_;
            }

            if (logger_) logger_->debug("[ConnAuth {}] Sending LOGON message (scheme: {}).", id_, params.auth_tokens.count("scheme") ? std::get<std::string>(params.auth_tokens.at("scheme")) : "unknown");

            err = send_request_receive_summary(logon_payload, out_success, out_failure);

            if (err == boltprotocol::BoltError::SUCCESS) {                   // This means summary was received and parsed
                if (last_error_code_ == boltprotocol::BoltError::SUCCESS) {  // Check if the summary was SUCCESS
                    _update_metadata_from_logon_success(out_success);
                    if (current_state_.load() != InternalState::DEFUNCT) {
                        current_state_.store(InternalState::READY);
                    }
                    if (logger_) logger_->info("[ConnAuth {}] LOGON successful.", id_);
                } else {  // Summary was FAILURE or IGNORED (which sets last_error_code_)
                    if (logger_) logger_->warn("[ConnAuth {}] LOGON server response not SUCCESS. Code: {}, Msg: {}", id_, static_cast<int>(last_error_code_), last_error_message_);
                }
            } else {  // send_request_receive_summary itself failed (e.g. network error)
                if (logger_) logger_->error("[ConnAuth {}] LOGON message send/receive summary failed. Error: {}", id_, static_cast<int>(err));
            }
            return last_error_code_;  // Return the final state of the connection
        }

        // perform_logon (sync) - (保持不变)
        boltprotocol::BoltError BoltPhysicalConnection::perform_logon(const boltprotocol::LogonMessageParams& logon_params, boltprotocol::SuccessMessageParams& out_success) {
            if (negotiated_bolt_version_ < boltprotocol::versions::V5_1) {
                last_error_code_ = boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION;
                last_error_message_ = "LOGON message not supported in Bolt version < 5.1";
                if (logger_) logger_->warn("[ConnAuth {}] {}", id_, last_error_message_);
                return last_error_code_;
            }

            InternalState current_s = current_state_.load();
            if (current_s != InternalState::HELLO_AUTH_SENT && current_s != InternalState::READY && current_s != InternalState::BOLT_HANDSHAKEN) {
                _mark_as_defunct_internal(boltprotocol::BoltError::UNKNOWN_ERROR, "perform_logon called in invalid state " + _get_current_state_as_string());
                if (logger_) logger_->warn("[ConnAuth {}] perform_logon in invalid state {}", id_, _get_current_state_as_string());
                return last_error_code_;
            }
            mark_as_used();

            boltprotocol::FailureMessageParams ignored_failure_details;  // Not used by caller, but filled by _execute
            return _execute_logon_message(logon_params, out_success, ignored_failure_details);
        }

        // perform_logoff (sync) - (保持不变)
        boltprotocol::BoltError BoltPhysicalConnection::perform_logoff(boltprotocol::SuccessMessageParams& out_success) {
            if (negotiated_bolt_version_ < boltprotocol::versions::V5_1) {
                last_error_code_ = boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION;
                last_error_message_ = "LOGOFF message not supported in Bolt version < 5.1";
                if (logger_) logger_->warn("[ConnAuth {}] {}", id_, last_error_message_);
                return last_error_code_;
            }
            if (!is_ready_for_queries()) {  // Checks state and stream validity
                std::string msg = "perform_logoff called when connection not ready. Current state: " + _get_current_state_as_string();
                _mark_as_defunct_internal(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->warn("[ConnAuth {}] {}", id_, msg);
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            mark_as_used();
            if (logger_) logger_->debug("[ConnAuth {}] Performing LOGOFF.", id_);

            std::vector<uint8_t> logoff_payload;
            boltprotocol::PackStreamWriter ps_writer(logoff_payload);
            boltprotocol::BoltError err = boltprotocol::serialize_logoff_message(ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct_internal(err, "LOGOFF serialization failed.");
                if (logger_) logger_->error("[ConnAuth {}] LOGOFF serialization failed: {}", id_, static_cast<int>(err));
                return last_error_code_;
            }

            boltprotocol::FailureMessageParams ignored_failure_details;
            err = send_request_receive_summary(logoff_payload, out_success, ignored_failure_details);

            if (err == boltprotocol::BoltError::SUCCESS && last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->info("[ConnAuth {}] LOGOFF successful.", id_);
                // After LOGOFF, server goes to AUTHENTICATION state (Bolt 5.1+)
                // For Bolt < 5.1 this message isn't sent.
                // Our internal state should reflect readiness for LOGON or closure.
                // HELLO_AUTH_SENT or BOLT_HANDSHAKEN seems appropriate pre-LOGON state.
                current_state_.store(InternalState::BOLT_HANDSHAKEN);
            } else {
                if (logger_) logger_->warn("[ConnAuth {}] LOGOFF failed. Error: {}, Server Msg: {}", id_, static_cast<int>(err), last_error_message_);
            }
            return last_error_code_;
        }

        // _execute_logon_message_async (async)
        boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> BoltPhysicalConnection::_execute_logon_message_async(boltprotocol::LogonMessageParams params,
                                                                                                                                                            std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref) {
            // (保持 Batch 9 中的实现不变，因为它调用的是静态辅助函数，而不是实例成员 _send/_receive_chunked_payload_async)
            // ...
            // 对于 _execute_logon_message_async 和 perform_logoff_async，它们应该使用实例成员的 _send_chunked_payload_async 和 _receive_chunked_payload_async，
            // 因为它们是 BoltPhysicalConnection 的一部分，并且操作的是传递给它们的特定流。
            // 但是，由于 establish_async 将流的所有权转移到 ActiveAsyncStreamContext，
            // 这些方法（如果它们要被 AsyncSessionHandle 调用）通常会变成静态的，并操作 ActiveAsyncStreamContext。
            // 既然它们是 BoltPhysicalConnection 的私有异步方法，被 perform_logon_async 和 perform_logoff_async（也是成员）调用，
            // 它们可以访问 this 指针，并应使用 this->_send_chunked_payload_async 等。
            //
            // 截图错误是在 perform_logoff_async 中，所以我们修复那里。
            // _execute_logon_message_async 也需要类似的修复。

            if (logger_) logger_->debug("[ConnAuthAsync {}] Executing LOGON message async (scheme: {}).", get_id_for_logging(), params.auth_tokens.count("scheme") ? std::get<std::string>(params.auth_tokens.at("scheme")) : "unknown");

            // Stream validity should be checked by the caller or at the beginning of a sequence
            // For instance methods, the stream_variant_ref is passed, and we operate on it.

            std::vector<uint8_t> logon_payload_storage;
            boltprotocol::PackStreamWriter ps_writer(logon_payload_storage);

            boltprotocol::BoltError err = boltprotocol::serialize_logon_message(params, ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                mark_as_defunct_from_async(err, "Async LOGON serialization failed.");
                if (logger_) logger_->error("[ConnAuthAsync {}] LOGON serialization failed: {}", get_id_for_logging(), static_cast<int>(err));
                co_return std::make_pair(last_error_code_, boltprotocol::SuccessMessageParams{});
            }

            boltprotocol::BoltError send_err = co_await this->_send_chunked_payload_async(async_stream_variant_ref, std::move(logon_payload_storage));  // Use 'this->'
            if (send_err != boltprotocol::BoltError::SUCCESS) {
                // mark_as_defunct_from_async is called by _send_chunked_payload_async if it fails via async_utils
                if (logger_) logger_->error("[ConnAuthAsync {}] Async LOGON send failed: {}", get_id_for_logging(), static_cast<int>(last_error_code_));
                co_return std::make_pair(last_error_code_, boltprotocol::SuccessMessageParams{});
            }

            boltprotocol::SuccessMessageParams success_meta;
            boltprotocol::FailureMessageParams failure_meta;
            boltprotocol::BoltError summary_err = boltprotocol::BoltError::UNKNOWN_ERROR;

            while (true) {
                auto [recv_err, response_payload] = co_await this->_receive_chunked_payload_async(async_stream_variant_ref);  // Use 'this->'
                if (recv_err != boltprotocol::BoltError::SUCCESS) {
                    summary_err = last_error_code_;  // _receive_chunked_payload_async marks defunct
                    break;
                }
                if (response_payload.empty()) {  // NOOP
                    if (logger_) logger_->trace("[ConnAuthAsync {}] Received NOOP while awaiting LOGON summary.", get_id_for_logging());
                    continue;
                }

                boltprotocol::MessageTag tag;
                boltprotocol::BoltError peek_err = _peek_message_tag(response_payload, tag);  // Instance member
                if (peek_err != boltprotocol::BoltError::SUCCESS) {
                    mark_as_defunct_from_async(peek_err, "Async LOGON: Failed to peek tag for summary response.");
                    summary_err = last_error_code_;
                    break;
                }

                boltprotocol::PackStreamReader reader(response_payload);
                if (tag == boltprotocol::MessageTag::SUCCESS) {
                    summary_err = boltprotocol::deserialize_success_message(reader, success_meta);
                    if (summary_err != boltprotocol::BoltError::SUCCESS) {
                        mark_as_defunct_from_async(summary_err, "Async LOGON: Failed to deserialize SUCCESS summary.");
                        summary_err = last_error_code_;
                    }
                    break;
                } else if (tag == boltprotocol::MessageTag::FAILURE) {
                    boltprotocol::BoltError deser_fail_err = boltprotocol::deserialize_failure_message(reader, failure_meta);
                    if (deser_fail_err != boltprotocol::BoltError::SUCCESS) {
                        mark_as_defunct_from_async(deser_fail_err, "Async LOGON: Failed to deserialize FAILURE summary.");
                        summary_err = last_error_code_;
                    } else {
                        summary_err = _classify_and_set_server_failure(failure_meta);  // This marks defunct if needed
                    }
                    break;
                } else if (tag == boltprotocol::MessageTag::IGNORED) {
                    mark_as_defunct_from_async(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Async LOGON: Received IGNORED instead of SUCCESS/FAILURE.");
                    summary_err = last_error_code_;
                    break;
                } else {
                    mark_as_defunct_from_async(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Async LOGON: Unexpected message tag " + std::to_string(static_cast<int>(tag)) + " for summary.");
                    summary_err = last_error_code_;
                    break;
                }
            }

            if (summary_err == boltprotocol::BoltError::SUCCESS && last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                _update_metadata_from_logon_success(success_meta);
                // Note: state update (e.g., to ASYNC_READY) is typically done by the caller (establish_async)
                // after successful HELLO and LOGON.
                if (logger_) logger_->info("[ConnAuthAsync {}] Async LOGON successful (intermediate step).", get_id_for_logging());
                co_return std::make_pair(boltprotocol::BoltError::SUCCESS, success_meta);
            } else {
                if (logger_) logger_->warn("[ConnAuthAsync {}] Async LOGON server response not SUCCESS. Final Error: {}, Server/Conn Msg: {}", get_id_for_logging(), static_cast<int>(last_error_code_), last_error_message_);
                co_return std::make_pair(last_error_code_, boltprotocol::SuccessMessageParams{});
            }
        }

        // perform_logon_async (async)
        boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> BoltPhysicalConnection::perform_logon_async(boltprotocol::LogonMessageParams logon_params,
                                                                                                                                                   std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref) {
            // (保持 Batch 9 中的实现不变，它调用的是 _execute_logon_message_async)
            if (negotiated_bolt_version_ < boltprotocol::versions::V5_1) {
                if (logger_) logger_->warn("[ConnAuthAsync {}] perform_logon_async: LOGON not supported in Bolt < 5.1", get_id_for_logging());
                co_return std::make_pair(boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION, boltprotocol::SuccessMessageParams{});
            }
            InternalState current_s = current_state_.load();
            // This method might be called during establish_async, so state can be ASYNC_BOLT_HANDSHAKEN
            if (current_s != InternalState::HELLO_AUTH_SENT && current_s != InternalState::ASYNC_HELLO_AUTH_SENT && current_s != InternalState::BOLT_HANDSHAKEN && current_s != InternalState::ASYNC_BOLT_HANDSHAKEN && current_s != InternalState::READY && current_s != InternalState::ASYNC_READY) {
                if (logger_) logger_->warn("[ConnAuthAsync {}] perform_logon_async in invalid state {}", get_id_for_logging(), _get_current_state_as_string());
                mark_as_defunct_from_async(boltprotocol::BoltError::UNKNOWN_ERROR, "perform_logon_async in invalid state " + _get_current_state_as_string());
                co_return std::make_pair(last_error_code_, boltprotocol::SuccessMessageParams{});
            }
            mark_as_used();  // This instance is used
            co_return co_await _execute_logon_message_async(std::move(logon_params), async_stream_variant_ref);
        }

        // perform_logoff_async (async) - 截图中的问题在这里
        boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> BoltPhysicalConnection::perform_logoff_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref) {
            if (negotiated_bolt_version_ < boltprotocol::versions::V5_1) {
                if (logger_) logger_->warn("[ConnAuthAsync {}] perform_logoff_async: LOGOFF not supported in Bolt < 5.1", get_id_for_logging());
                co_return std::make_pair(boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION, boltprotocol::SuccessMessageParams{});
            }
            InternalState current_s = current_state_.load();
            // LOGOFF is usually from READY or ASYNC_READY state (when a connection is actively being used by a session)
            // It's less common during initial establishment, but check relevant states.
            if (current_s != InternalState::READY && current_s != InternalState::ASYNC_READY && current_s != InternalState::STREAMING && current_s != InternalState::ASYNC_STREAMING && /* Added streaming states */
                current_s != InternalState::AWAITING_SUMMARY && current_s != InternalState::ASYNC_AWAITING_SUMMARY) {
                if (logger_) logger_->warn("[ConnAuthAsync {}] perform_logoff_async called when not ready/streaming. State: {}", get_id_for_logging(), _get_current_state_as_string());
                mark_as_defunct_from_async(boltprotocol::BoltError::UNKNOWN_ERROR, "perform_logoff_async in invalid state " + _get_current_state_as_string());
                co_return std::make_pair(last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR, boltprotocol::SuccessMessageParams{});
            }
            mark_as_used();
            if (logger_) logger_->debug("[ConnAuthAsync {}] Performing LOGOFF async.", get_id_for_logging());

            std::vector<uint8_t> logoff_payload_storage;
            boltprotocol::PackStreamWriter ps_writer(logoff_payload_storage);
            boltprotocol::BoltError err = boltprotocol::serialize_logoff_message(ps_writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                mark_as_defunct_from_async(err, "Async LOGOFF serialization failed.");
                if (logger_) logger_->error("[ConnAuthAsync {}] LOGOFF serialization failed: {}", get_id_for_logging(), static_cast<int>(err));
                co_return std::make_pair(last_error_code_, boltprotocol::SuccessMessageParams{});
            }

            boltprotocol::BoltError send_err = co_await this->_send_chunked_payload_async(async_stream_variant_ref, std::move(logoff_payload_storage));  // Use 'this->'
            if (send_err != boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->error("[ConnAuthAsync {}] Async LOGOFF send failed: {}", get_id_for_logging(), static_cast<int>(last_error_code_));
                co_return std::make_pair(last_error_code_, boltprotocol::SuccessMessageParams{});
            }

            boltprotocol::SuccessMessageParams success_meta;
            boltprotocol::FailureMessageParams failure_meta;
            boltprotocol::BoltError summary_err = boltprotocol::BoltError::UNKNOWN_ERROR;

            while (true) {
                // Corrected call: use 'this->' for instance member
                auto [recv_err, response_payload] = co_await this->_receive_chunked_payload_async(async_stream_variant_ref);
                if (recv_err != boltprotocol::BoltError::SUCCESS) {
                    summary_err = last_error_code_;
                    break;
                }
                if (response_payload.empty()) continue;  // NOOP

                boltprotocol::MessageTag tag;
                boltprotocol::BoltError peek_err = _peek_message_tag(response_payload, tag);
                if (peek_err != boltprotocol::BoltError::SUCCESS) {
                    mark_as_defunct_from_async(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Async LOGOFF: Peek tag failed.");
                    summary_err = last_error_code_;
                    break;
                }
                boltprotocol::PackStreamReader reader(response_payload);

                if (tag == boltprotocol::MessageTag::SUCCESS) {
                    summary_err = boltprotocol::deserialize_success_message(reader, success_meta);
                    if (summary_err != boltprotocol::BoltError::SUCCESS) mark_as_defunct_from_async(summary_err, "Async LOGOFF: Failed to deserialize SUCCESS.");
                    break;
                } else if (tag == boltprotocol::MessageTag::FAILURE) {
                    summary_err = boltprotocol::deserialize_failure_message(reader, failure_meta);
                    if (summary_err == boltprotocol::BoltError::SUCCESS)
                        summary_err = _classify_and_set_server_failure(failure_meta);
                    else
                        mark_as_defunct_from_async(summary_err, "Async LOGOFF: Failed to deserialize FAILURE.");
                    break;
                } else {  // IGNORED or other unexpected
                    mark_as_defunct_from_async(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Async LOGOFF: Unexpected summary tag.");
                    summary_err = last_error_code_;
                    break;
                }
            }

            if (summary_err == boltprotocol::BoltError::SUCCESS && last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->info("[ConnAuthAsync {}] Async LOGOFF successful.", get_id_for_logging());
                // After successful LOGOFF, transition to a state ready for LOGON
                // This should align with the server state transition to AUTHENTICATION
                current_state_.store(InternalState::ASYNC_BOLT_HANDSHAKEN);  // Or BOLT_HANDSHAKEN if it was from sync state
            } else {
                if (logger_) logger_->warn("[ConnAuthAsync {}] Async LOGOFF failed. Error: {}, Server Msg: {}", get_id_for_logging(), static_cast<int>(last_error_code_), last_error_message_);
            }
            co_return std::make_pair(last_error_code_, success_meta);  // Return last_error_code_ and success_meta (which might be empty if failure)
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport