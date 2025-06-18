#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <variant>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/async_types.h"
#include "neo4j_bolt_transport/internal/bolt_connection_config.h"  // Ensure this is included
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>> BoltPhysicalConnection::establish_async() {
            InternalState expected_fresh = InternalState::FRESH;
            // Use a temporary BoltConnectionConfig that will be moved into the ActiveAsyncStreamContext
            // This config is initialized from this->conn_config_ which is the one passed to BoltPhysicalConnection constructor.
            BoltConnectionConfig current_op_config = this->conn_config_;  // Make a copy for this operation

            if (!current_state_.compare_exchange_strong(expected_fresh, InternalState::ASYNC_TCP_CONNECTING, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                InternalState current_s = current_state_.load(std::memory_order_relaxed);
                std::string msg = "Establish_async called in invalid state: " + _get_current_state_as_string() + ". Expected FRESH.";
                if (current_s == InternalState::ASYNC_READY || current_s == InternalState::READY) {
                    msg = "Establish_async called but connection is already READY. Cannot provide a new ActiveAsyncStreamContext.";
                    if (logger_) logger_->warn("[ConnLCAsync {}] {}", get_id_for_logging(), msg);
                    // Return an error and a default-constructed (but invalid) context
                    mark_as_defunct_from_async(boltprotocol::BoltError::UNKNOWN_ERROR, msg);  // Mark this instance as defunct
                    co_return std::make_pair(last_error_code_, ActiveAsyncStreamContext(io_context_ref_));
                }
                if (logger_) logger_->warn("[ConnLCAsync {}] {}", get_id_for_logging(), msg);
                co_return std::make_pair((current_s == InternalState::DEFUNCT) ? last_error_code_ : boltprotocol::BoltError::UNKNOWN_ERROR, ActiveAsyncStreamContext(io_context_ref_));
            }

            if (logger_) logger_->info("[ConnLCAsync {}] Establishing (async) connection to {}:{}", get_id_for_logging(), current_op_config.target_host, current_op_config.target_port);

            _reset_resources_and_state(false);  // Resets this BoltPhysicalConnection instance
            current_state_.store(InternalState::ASYNC_TCP_CONNECTING, std::memory_order_relaxed);

            boost::asio::ip::tcp::socket temp_socket(io_context_ref_);
            boltprotocol::BoltError err = boltprotocol::BoltError::UNKNOWN_ERROR;

            err = co_await _stage_tcp_connect_async(temp_socket, std::chrono::milliseconds(current_op_config.tcp_connect_timeout_ms));
            if (err != boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->error("[ConnLCAsync {}] Async TCP connect stage failed: {}", get_id_for_logging(), error::bolt_error_to_string(err));
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                co_return std::make_pair(last_error_code_, ActiveAsyncStreamContext(io_context_ref_));
            }

            std::variant<boost::asio::ip::tcp::socket, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> final_stream_variant{std::move(temp_socket)};
            bool encryption_used_for_context = false;

            if (current_op_config.encryption_enabled) {
                encryption_used_for_context = true;
                // _stage_ssl_context_setup uses this->conn_config_ (which is current_op_config now for this instance)
                err = _stage_ssl_context_setup();
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _reset_resources_and_state(false);
                    current_state_.store(InternalState::FRESH);
                    co_return std::make_pair(last_error_code_, ActiveAsyncStreamContext(io_context_ref_));
                }

                boost::asio::ip::tcp::socket plain_socket_for_ssl = std::get<boost::asio::ip::tcp::socket>(std::move(final_stream_variant));
                boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket_stream(std::move(plain_socket_for_ssl), *ssl_context_sync_);

                current_state_.store(InternalState::ASYNC_SSL_HANDSHAKING);
                err = co_await _stage_ssl_handshake_async(ssl_socket_stream, std::chrono::milliseconds(current_op_config.bolt_handshake_timeout_ms));
                if (err != boltprotocol::BoltError::SUCCESS) {
                    if (logger_) logger_->error("[ConnLCAsync {}] Async SSL handshake stage failed: {}", get_id_for_logging(), error::bolt_error_to_string(err));
                    _reset_resources_and_state(false);
                    current_state_.store(InternalState::FRESH);
                    co_return std::make_pair(last_error_code_, ActiveAsyncStreamContext(io_context_ref_));
                }
                final_stream_variant = std::move(ssl_socket_stream);
                current_state_.store(InternalState::SSL_HANDSHAKEN);  // This instance's state
            }

            std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*> active_async_stream_ptr_variant;
            if (std::holds_alternative<boost::asio::ip::tcp::socket>(final_stream_variant)) {
                active_async_stream_ptr_variant = &std::get<boost::asio::ip::tcp::socket>(final_stream_variant);
            } else {
                active_async_stream_ptr_variant = &std::get<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(final_stream_variant);
            }

            // _stage_bolt_handshake_async will use this->negotiated_bolt_version_
            current_state_.store(InternalState::ASYNC_BOLT_HANDSHAKING);
            err = co_await _stage_bolt_handshake_async(active_async_stream_ptr_variant, std::chrono::milliseconds(current_op_config.bolt_handshake_timeout_ms));
            if (err != boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->error("[ConnLCAsync {}] Async Bolt handshake stage failed: {}", get_id_for_logging(), error::bolt_error_to_string(err));
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                co_return std::make_pair(last_error_code_, ActiveAsyncStreamContext(io_context_ref_));
            }
            current_state_.store(InternalState::ASYNC_BOLT_HANDSHAKEN);

            // _stage_send_hello_and_initial_auth_async will use this->server_agent_string_ etc.
            current_state_.store(InternalState::ASYNC_HELLO_AUTH_SENT);
            err = co_await _stage_send_hello_and_initial_auth_async(active_async_stream_ptr_variant);
            if (err != boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->error("[ConnLCAsync {}] Async HELLO/Auth stage failed: {}", get_id_for_logging(), error::bolt_error_to_string(err));
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                co_return std::make_pair(last_error_code_, ActiveAsyncStreamContext(io_context_ref_));
            }
            // After this, this->negotiated_bolt_version_, this->server_agent_string_ etc. are set

            InternalState final_state_of_this_conn = current_state_.load(std::memory_order_relaxed);
            if (final_state_of_this_conn != InternalState::ASYNC_READY && final_state_of_this_conn != InternalState::READY) {
                std::string msg = "Async connection did not reach READY/ASYNC_READY state after successful establish sequence. Final state: " + _get_current_state_as_string();
                if (logger_) logger_->error("[ConnLCAsync {}] {}", get_id_for_logging(), msg);
                mark_as_defunct_from_async(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                co_return std::make_pair(last_error_code_, ActiveAsyncStreamContext(io_context_ref_));
            }

            // For this BoltPhysicalConnection instance, mark as used and clear errors
            mark_as_used();
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->info("[ConnLCAsync {}] Async Connection (underlying establish logic) complete and ready. Bolt version: {}.{}. Server: {}", get_id_for_logging(), (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor, server_agent_string_);

            // Construct the ActiveAsyncStreamContext to be returned, moving the stream and copying relevant data
            ActiveAsyncStreamContext async_context_to_return(std::move(final_stream_variant),
                                                             std::move(current_op_config),  // Move the config copy
                                                             this->negotiated_bolt_version_,
                                                             this->server_agent_string_,
                                                             this->server_assigned_conn_id_,
                                                             this->utc_patch_active_,
                                                             encryption_used_for_context);

            // This BoltPhysicalConnection instance is now just a factory. Its internal stream is gone.
            // Reset its state to FRESH as it no longer holds an active stream.
            _reset_resources_and_state(false);  // Clean up unique_ptrs like ssl_context_sync_
            current_state_.store(InternalState::FRESH);

            co_return std::make_pair(boltprotocol::BoltError::SUCCESS, std::move(async_context_to_return));
        }

        // terminate_async and ping_async remain as placeholders as they operate on the instance's stream,
        // which is not the model for ActiveAsyncStreamContext.
        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::terminate_async(bool send_goodbye) {
            InternalState previous_state = current_state_.exchange(InternalState::DEFUNCT, std::memory_order_acq_rel);
            if (logger_) {
                logger_->debug("[ConnLCAsync {}] Terminating (async). Previous state was {}. Send goodbye: {}", get_id_for_logging(), (previous_state == InternalState::DEFUNCT ? "already DEFUNCT" : _get_current_state_as_string()), send_goodbye);
            }

            if (previous_state == InternalState::DEFUNCT) {
                _reset_resources_and_state(false);
                co_return boltprotocol::BoltError::SUCCESS;
            }
            // GOODBYE for an instance's stream is complex if the stream is now managed by ActiveAsyncStreamContext.
            // This terminate_async is more for the pooled connection concept.
            _reset_resources_and_state(false);
            co_return boltprotocol::BoltError::SUCCESS;
        }

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::ping_async(std::chrono::milliseconds timeout) {
            if (logger_) logger_->debug("[ConnLCAsync {}] Pinging (async) connection (via async RESET). Timeout hint: {}ms", get_id_for_logging(), timeout.count());
            if (logger_) logger_->error("[ConnLCAsync {}] ping_async is a placeholder and requires an active async stream context to perform a true async RESET.", get_id_for_logging());
            // This would need to use send_request_receive_summary_async_static with a RESET payload.
            co_return boltprotocol::BoltError::UNKNOWN_ERROR;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport