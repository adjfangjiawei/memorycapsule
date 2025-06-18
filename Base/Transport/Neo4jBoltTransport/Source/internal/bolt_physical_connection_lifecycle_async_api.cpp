#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <variant>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/async_types.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>> BoltPhysicalConnection::establish_async() {
            InternalState expected_fresh = InternalState::FRESH;
            if (!current_state_.compare_exchange_strong(expected_fresh, InternalState::ASYNC_TCP_CONNECTING, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                InternalState current_s = current_state_.load(std::memory_order_relaxed);
                if (current_s == InternalState::ASYNC_READY || current_s == InternalState::READY) {
                    if (logger_) logger_->debug("[ConnLCAsync {}] Establish_async called but connection is already READY.", get_id_for_logging());
                    std::string msg = "Establish_async called but connection is already READY. Cannot provide a new ActiveAsyncStreamContext.";
                    if (logger_) logger_->warn("[ConnLCAsync {}] {}", get_id_for_logging(), msg);
                    mark_as_defunct_from_async(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                    co_return std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>{last_error_code_, ActiveAsyncStreamContext(io_context_ref_)};
                }
                std::string msg = "Establish_async called in invalid state: " + _get_current_state_as_string() + ". Expected FRESH.";
                if (logger_) logger_->warn("[ConnLCAsync {}] {}", get_id_for_logging(), msg);
                co_return std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>{(current_s == InternalState::DEFUNCT) ? last_error_code_ : boltprotocol::BoltError::UNKNOWN_ERROR, ActiveAsyncStreamContext(io_context_ref_)};
            }

            if (logger_) logger_->info("[ConnLCAsync {}] Establishing (async) connection to {}:{}", get_id_for_logging(), conn_config_.target_host, conn_config_.target_port);

            _reset_resources_and_state(false);
            current_state_.store(InternalState::ASYNC_TCP_CONNECTING, std::memory_order_relaxed);

            boost::asio::ip::tcp::socket temp_socket(io_context_ref_);  // Socket will be moved
            boltprotocol::BoltError err = boltprotocol::BoltError::UNKNOWN_ERROR;

            err = co_await _stage_tcp_connect_async(temp_socket, std::chrono::milliseconds(conn_config_.tcp_connect_timeout_ms));
            if (err != boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->error("[ConnLCAsync {}] Async TCP connect stage failed: {}", get_id_for_logging(), error::bolt_error_to_string(err));
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                co_return std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>{last_error_code_, ActiveAsyncStreamContext(io_context_ref_)};
            }

            std::variant<boost::asio::ip::tcp::socket, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> final_stream_variant{std::move(temp_socket)};
            bool encryption_used_for_context = false;

            if (conn_config_.encryption_enabled) {
                encryption_used_for_context = true;
                err = _stage_ssl_context_setup();
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _reset_resources_and_state(false);
                    current_state_.store(InternalState::FRESH);
                    co_return std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>{last_error_code_, ActiveAsyncStreamContext(io_context_ref_)};
                }

                // temp_socket has been moved into final_stream_variant.
                // We need to extract it, construct ssl_socket_stream, and then move ssl_socket_stream back into final_stream_variant.
                boost::asio::ip::tcp::socket plain_socket_for_ssl = std::get<boost::asio::ip::tcp::socket>(std::move(final_stream_variant));
                // Create SSL stream on the stack or as a local unique_ptr before moving
                boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket_stream(std::move(plain_socket_for_ssl), *ssl_context_sync_);

                current_state_.store(InternalState::ASYNC_SSL_HANDSHAKING);
                // _stage_ssl_handshake_async's first parameter should be boost::asio::ssl::stream<boost::asio::ip::tcp::socket>&
                err = co_await _stage_ssl_handshake_async(ssl_socket_stream, std::chrono::milliseconds(conn_config_.bolt_handshake_timeout_ms));
                if (err != boltprotocol::BoltError::SUCCESS) {
                    if (logger_) logger_->error("[ConnLCAsync {}] Async SSL handshake stage failed: {}", get_id_for_logging(), error::bolt_error_to_string(err));
                    _reset_resources_and_state(false);
                    current_state_.store(InternalState::FRESH);
                    co_return std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>{last_error_code_, ActiveAsyncStreamContext(io_context_ref_)};
                }
                final_stream_variant = std::move(ssl_socket_stream);
                current_state_.store(InternalState::SSL_HANDSHAKEN);
            }

            std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*> active_async_stream_ptr_variant;
            if (std::holds_alternative<boost::asio::ip::tcp::socket>(final_stream_variant)) {
                active_async_stream_ptr_variant = &std::get<boost::asio::ip::tcp::socket>(final_stream_variant);
            } else {
                active_async_stream_ptr_variant = &std::get<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(final_stream_variant);
            }

            current_state_.store(InternalState::ASYNC_BOLT_HANDSHAKING);
            err = co_await _stage_bolt_handshake_async(active_async_stream_ptr_variant, std::chrono::milliseconds(conn_config_.bolt_handshake_timeout_ms));
            if (err != boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->error("[ConnLCAsync {}] Async Bolt handshake stage failed: {}", get_id_for_logging(), error::bolt_error_to_string(err));
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                co_return std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>{last_error_code_, ActiveAsyncStreamContext(io_context_ref_)};
            }
            current_state_.store(InternalState::ASYNC_BOLT_HANDSHAKEN);

            current_state_.store(InternalState::ASYNC_HELLO_AUTH_SENT);
            err = co_await _stage_send_hello_and_initial_auth_async(active_async_stream_ptr_variant);
            if (err != boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->error("[ConnLCAsync {}] Async HELLO/Auth stage failed: {}", get_id_for_logging(), error::bolt_error_to_string(err));
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                co_return std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>{last_error_code_, ActiveAsyncStreamContext(io_context_ref_)};
            }

            if (current_state_.load(std::memory_order_relaxed) != InternalState::ASYNC_READY && current_state_.load(std::memory_order_relaxed) != InternalState::READY) {
                std::string msg = "Async connection did not reach READY/ASYNC_READY state after successful establish sequence. Final state: " + _get_current_state_as_string();
                if (logger_) logger_->error("[ConnLCAsync {}] {}", get_id_for_logging(), msg);
                mark_as_defunct_from_async(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                co_return std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>{last_error_code_, ActiveAsyncStreamContext(io_context_ref_)};
            }

            mark_as_used();
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->info("[ConnLCAsync {}] Async Connection established and ready. Bolt version: {}.{}. Server: {}", get_id_for_logging(), (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor, server_agent_string_);

            ActiveAsyncStreamContext async_context(std::move(final_stream_variant), negotiated_bolt_version_, server_agent_string_, server_assigned_conn_id_, utc_patch_active_, encryption_used_for_context);
            co_return std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>{boltprotocol::BoltError::SUCCESS, std::move(async_context)};
        }

        // terminate_async and ping_async保持不变
        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::terminate_async(bool send_goodbye) {
            InternalState previous_state = current_state_.exchange(InternalState::DEFUNCT, std::memory_order_acq_rel);
            if (logger_) {
                logger_->debug("[ConnLCAsync {}] Terminating (async). Previous state was {}. Send goodbye: {}", get_id_for_logging(), (int)previous_state, send_goodbye);
            }

            if (previous_state == InternalState::DEFUNCT) {
                _reset_resources_and_state(false);
                co_return boltprotocol::BoltError::SUCCESS;
            }

            if (send_goodbye && previous_state >= InternalState::ASYNC_BOLT_HANDSHAKEN && previous_state < InternalState::DEFUNCT && !(negotiated_bolt_version_ < boltprotocol::versions::Version(3, 0))) {
                if (logger_) logger_->trace("[ConnLCAsync {}] Async GOODBYE logic placeholder: This operation would require the ActiveAsyncStreamContext to be passed or managed by the caller of terminate_async. Not sending GOODBYE from BoltPhysicalConnection directly.", get_id_for_logging());
            }

            _reset_resources_and_state(false);
            co_return boltprotocol::BoltError::SUCCESS;
        }

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::ping_async(std::chrono::milliseconds timeout) {
            if (logger_) logger_->debug("[ConnLCAsync {}] Pinging (async) connection (via async RESET). Timeout hint: {}ms", get_id_for_logging(), timeout.count());
            if (logger_) logger_->error("[ConnLCAsync {}] ping_async is a placeholder and requires an active async stream context to perform a true async RESET.", get_id_for_logging());
            co_return boltprotocol::BoltError::UNKNOWN_ERROR;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport