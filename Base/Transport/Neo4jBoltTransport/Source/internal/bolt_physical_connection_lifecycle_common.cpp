#include <openssl/ssl.h>  // For SSL_get_shutdown, SSL_RECEIVED_SHUTDOWN, SSL_SENT_SHUTDOWN (needed by _reset_resources_and_state)

#include <boost/asio/ip/tcp.hpp>  // For socket::shutdown_both

#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        void BoltPhysicalConnection::_reset_resources_and_state(bool called_from_destructor) {
            // This function is critical for cleaning up resources.
            // It's called from establish() before new attempt, terminate(), destructor, and move assignment.

            // 1. Close SSL stream (if exists and open)
            if (ssl_stream_sync_) {
                boost::system::error_code ec_ssl_shutdown, ec_tcp_close;
                if (ssl_stream_sync_->lowest_layer().is_open()) {
                    // Attempt graceful SSL shutdown only if not already shut down by peer.
                    // SSL_get_shutdown checks SSL state.
                    if (!(SSL_get_shutdown(ssl_stream_sync_->native_handle()) & (SSL_RECEIVED_SHUTDOWN | SSL_SENT_SHUTDOWN))) {
                        try {
                            ssl_stream_sync_->shutdown(ec_ssl_shutdown);                                                                                                     // Graceful SSL shutdown
                            if (logger_ && ec_ssl_shutdown && ec_ssl_shutdown != boost::asio::error::eof && ec_ssl_shutdown != boost::asio::ssl::error::stream_truncated) {  // EOF/truncated is common on close
                                logger_->trace("[ConnReset {}] SSL shutdown error: {}", id_, ec_ssl_shutdown.message());
                            }
                        } catch (const boost::system::system_error& e) {
                            if (logger_) logger_->trace("[ConnReset {}] Exception during SSL stream shutdown: {}", id_, e.what());
                        }
                    }
                    // Close the underlying socket
                    try {
                        ssl_stream_sync_->lowest_layer().close(ec_tcp_close);
                        if (logger_ && ec_tcp_close) {
                            logger_->trace("[ConnReset {}] SSL lowest_layer close error: {}", id_, ec_tcp_close.message());
                        }
                    } catch (const boost::system::system_error& e) {
                        if (logger_) logger_->trace("[ConnReset {}] Exception during SSL lowest_layer close: {}", id_, e.what());
                    }
                }
                ssl_stream_sync_.reset();  // Release the unique_ptr
            }

            // 2. Reset SSL context (if exists)
            if (ssl_context_sync_) {
                ssl_context_sync_.reset();
            }

            // 3. Close plain iostream wrapper (if exists, it owns its socket if not SSL)
            if (plain_iostream_wrapper_) {
                // Destroying iostream wrapper will close its associated socket if it has one.
                plain_iostream_wrapper_.reset();
            }

            // 4. Close the raw owned socket (if it still exists and wasn't moved to iostream/ssl_stream)
            if (owned_socket_for_sync_plain_) {
                if (owned_socket_for_sync_plain_->is_open()) {
                    boost::system::error_code ec_shutdown, ec_close;
                    try {
                        owned_socket_for_sync_plain_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec_shutdown);
                        if (logger_ && ec_shutdown) {
                            logger_->trace("[ConnReset {}] Plain socket shutdown error: {}", id_, ec_shutdown.message());
                        }
                    } catch (const boost::system::system_error& e) {
                        if (logger_) logger_->trace("[ConnReset {}] Exception during plain socket shutdown: {}", id_, e.what());
                    }
                    try {
                        owned_socket_for_sync_plain_->close(ec_close);
                        if (logger_ && ec_close) {
                            logger_->trace("[ConnReset {}] Plain socket close error: {}", id_, ec_close.message());
                        }
                    } catch (const boost::system::system_error& e) {
                        if (logger_) logger_->trace("[ConnReset {}] Exception during plain socket close: {}", id_, e.what());
                    }
                }
                owned_socket_for_sync_plain_.reset();
            }

            // 5. Reset Bolt protocol specific state
            negotiated_bolt_version_ = boltprotocol::versions::Version(0, 0);
            server_agent_string_.clear();
            server_assigned_conn_id_.clear();
            utc_patch_active_ = false;

            // 6. Reset error state, unless in destructor of an already defunct connection
            //    or if we want to preserve the "original sin" error.
            //    For a full reset to FRESH for reuse, error should be cleared.
            if (!called_from_destructor || current_state_.load(std::memory_order_relaxed) != InternalState::DEFUNCT) {
                last_error_code_ = boltprotocol::BoltError::SUCCESS;
                last_error_message_.clear();
            }

            // 7. Set state to FRESH, unless in destructor and already DEFUNCT (don't revive it)
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (!called_from_destructor || current_s != InternalState::DEFUNCT) {
                current_state_.store(InternalState::FRESH, std::memory_order_relaxed);
            }

            if (logger_) logger_->trace("[ConnReset {}] Resources and state reset. Current state for reuse (if not dtor): {}", id_, _get_current_state_as_string());
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport