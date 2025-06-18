#include <atomic>  // For std::atomic, std::memory_order_*
#include <boost/asio/detached.hpp>
#include <iostream>

#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"  // For send_goodbye_async_static
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

// Conditional include for OpenSSL headers
#ifdef __has_include
#if __has_include(<openssl/ssl.h>)
#include <openssl/ssl.h>
#define HAS_OPENSSL_SSL_H_ASYNC_SESSION  // Define a specific macro for this file
#endif
#endif

namespace neo4j_bolt_transport {

    AsyncSessionHandle::AsyncSessionHandle(Neo4jBoltTransport* transport_mgr, config::SessionParameters params, std::unique_ptr<internal::ActiveAsyncStreamContext> stream_ctx)
        : transport_manager_(transport_mgr),
          session_params_(std::move(params)),
          stream_context_(std::move(stream_ctx)),
          is_closed_(false),       // Initialize std::atomic<bool>
          close_initiated_(false)  // Initialize std::atomic<bool>
    {
        if (!transport_manager_) {
            last_error_code_ = boltprotocol::BoltError::INVALID_ARGUMENT;
            last_error_message_ = "AsyncSessionHandle created with null transport_manager.";
            is_closed_.store(true, std::memory_order_release);
            close_initiated_.store(true, std::memory_order_release);
            std::cerr << "CRITICAL: " << last_error_message_ << std::endl;
        } else if (!stream_context_ || !std::visit(
                                           [](auto& s_ref) {
                                               return s_ref.lowest_layer().is_open();
                                           },
                                           stream_context_->stream)) {
            last_error_code_ = boltprotocol::BoltError::NETWORK_ERROR;
            last_error_message_ = "AsyncSessionHandle created with invalid or closed stream_context.";
            is_closed_.store(true, std::memory_order_release);
            close_initiated_.store(true, std::memory_order_release);
            if (transport_manager_->get_config().logger) {
                transport_manager_->get_config().logger->error("[AsyncSessionLC] {}", last_error_message_);
            }
        } else {
            if (transport_manager_->get_config().logger) {
                transport_manager_->get_config().logger->debug("[AsyncSessionLC] AsyncSessionHandle created for DB '{}', server '{}', conn_id '{}'", session_params_.database_name.value_or("<default>"), stream_context_->server_agent_string, stream_context_->server_connection_id);
            }
        }
    }

    AsyncSessionHandle::~AsyncSessionHandle() {
        // Use acquire for load as we want to see the latest value from other threads
        if (!is_closed_.load(std::memory_order_acquire)) {
            std::shared_ptr<spdlog::logger> logger = nullptr;
            if (transport_manager_ && transport_manager_->get_config().logger) {
                logger = transport_manager_->get_config().logger;
            }
            if (logger) {
                logger->warn("[AsyncSessionLC] AsyncSessionHandle destructed without explicit close_async(). Forcing synchronous-like closure for stream context '{}'. This might block if called from non-asio thread or lead to issues.",
                             stream_context_ ? stream_context_->server_connection_id : "N/A");
            }
            // Best-effort synchronous close in destructor
            if (stream_context_ && std::visit(
                                       [](auto& s_variant_ref) {
                                           return s_variant_ref.lowest_layer().is_open();
                                       },
                                       stream_context_->stream)) {
                std::visit(
                    [](auto& s_variant_ref) {  // Pass by reference
                        boost::system::error_code ec;
                        // Note: Cannot perform async_shutdown in a destructor without running an io_context.
                        // This is a best-effort close of the underlying socket.
                        if constexpr (std::is_same_v<std::decay_t<decltype(s_variant_ref)>, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>) {
                            // SSL graceful shutdown is complex to do synchronously without potential blocking
                            // or needing to run the io_context.
                        }
                        s_variant_ref.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                        s_variant_ref.lowest_layer().close(ec);
                    },
                    stream_context_->stream);
            }
            // Mark as closed after attempting to close resources
            is_closed_.store(true, std::memory_order_release);
            close_initiated_.store(true, std::memory_order_release);  // If destructor runs, close is effectively initiated and done
        }
    }

    AsyncSessionHandle::AsyncSessionHandle(AsyncSessionHandle&& other) noexcept
        : transport_manager_(other.transport_manager_),
          session_params_(std::move(other.session_params_)),
          stream_context_(std::move(other.stream_context_)),
          // Atomically load the state from 'other' and store it in 'this'
          is_closed_(other.is_closed_.load(std::memory_order_acquire)),
          close_initiated_(other.close_initiated_.load(std::memory_order_acquire)),
          last_error_code_(other.last_error_code_),
          last_error_message_(std::move(other.last_error_message_)) {
        other.transport_manager_ = nullptr;
        // Mark 'other' as closed so its destructor doesn't try to manage resources
        other.is_closed_.store(true, std::memory_order_release);
        other.close_initiated_.store(true, std::memory_order_release);
    }

    AsyncSessionHandle& AsyncSessionHandle::operator=(AsyncSessionHandle&& other) noexcept {
        if (this != &other) {
            // Clean up current resources if this session is active
            if (!is_closed_.load(std::memory_order_acquire) && stream_context_) {
                std::visit(
                    [](auto& s_variant_ref) {
                        boost::system::error_code ec;
                        if constexpr (std::is_same_v<std::decay_t<decltype(s_variant_ref)>, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>) {
                            // Best effort SSL shutdown
                        }
                        s_variant_ref.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                        s_variant_ref.lowest_layer().close(ec);
                    },
                    stream_context_->stream);
            }

            transport_manager_ = other.transport_manager_;
            session_params_ = std::move(other.session_params_);
            stream_context_ = std::move(other.stream_context_);
            // Atomically transfer state
            is_closed_.store(other.is_closed_.load(std::memory_order_acquire), std::memory_order_release);
            close_initiated_.store(other.close_initiated_.load(std::memory_order_acquire), std::memory_order_release);
            last_error_code_ = other.last_error_code_;
            last_error_message_ = std::move(other.last_error_message_);

            other.transport_manager_ = nullptr;
            other.is_closed_.store(true, std::memory_order_release);
            other.close_initiated_.store(true, std::memory_order_release);
        }
        return *this;
    }

    void AsyncSessionHandle::mark_closed() {
        is_closed_.store(true, std::memory_order_release);
        // If mark_closed is called, it implies a close operation has been initiated or completed.
        close_initiated_.store(true, std::memory_order_release);
    }

    bool AsyncSessionHandle::is_valid() const {
        return !is_closed_.load(std::memory_order_acquire) && stream_context_ != nullptr &&
               std::visit(
                   [](const auto& s_ref) {
                       return s_ref.lowest_layer().is_open();
                   },
                   stream_context_->stream);
    }

    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::send_goodbye_if_appropriate_async() {
        if (!is_valid() || !stream_context_) {  // Added stream_context_ check for safety
            co_return boltprotocol::BoltError::SUCCESS;
        }
        if (!transport_manager_) {
            // Cannot get logger or config if transport_manager_ is null.
            // This indicates a more severe setup problem.
            // It's hard to log here without logger access.
            // Returning an error is the best course.
            co_return boltprotocol::BoltError::INVALID_ARGUMENT;
        }

        if (stream_context_->negotiated_bolt_version < boltprotocol::versions::Version(3, 0)) {
            co_return boltprotocol::BoltError::SUCCESS;
        }

        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }
        if (logger) logger->trace("[AsyncSessionLC] Sending GOODBYE for connection id '{}'", stream_context_->server_connection_id);

        auto goodbye_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            if (logger_copy) {
                logger_copy->warn("[AsyncSessionLC] Error during static GOODBYE send for conn_id '{}': {} - {}", stream_context_ ? stream_context_->server_connection_id : "N/A", static_cast<int>(reason), message);
            }
            // This error handler is for send_goodbye_async_static internal errors.
            // The AsyncSessionHandle's last_error_ might be updated by close_async if this fails.
        };

        co_return co_await internal::BoltPhysicalConnection::send_goodbye_async_static(*stream_context_,
                                                                                       stream_context_->original_config,  // Pass the BoltConnectionConfig from the context
                                                                                       logger,
                                                                                       goodbye_error_handler);
    }

    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::close_async() {
        bool already_initiated = close_initiated_.exchange(true, std::memory_order_acq_rel);
        // Check is_closed_ with acquire to see writes from other threads.
        if (already_initiated || is_closed_.load(std::memory_order_acquire)) {
            co_return last_error_code_;  // Return last known error if already closed/closing
        }

        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        std::string conn_id_for_log = (stream_context_ ? stream_context_->server_connection_id : "N/A");
        if (logger) logger->debug("[AsyncSessionLC] close_async called for session with server connection id '{}'.", conn_id_for_log);

        if (is_valid() && stream_context_) {  // Ensure stream_context_ is not null before visiting
            co_await send_goodbye_if_appropriate_async();

            std::visit(
                [logger_copy = logger, log_cid = conn_id_for_log](auto& s_variant_ref) {
                    boost::system::error_code ec_shutdown, ec_close;
                    if (s_variant_ref.lowest_layer().is_open()) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(s_variant_ref)>, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>) {
#ifdef HAS_OPENSSL_SSL_H_ASYNC_SESSION  // Use file-specific macro
                            // Check if SSL shutdown has already occurred or been initiated by peer.
                            if (!(SSL_get_shutdown(s_variant_ref.native_handle()) & (SSL_RECEIVED_SHUTDOWN | SSL_SENT_SHUTDOWN))) {
                                // Fire and forget SSL shutdown. Errors are ignored as we are closing anyway.
                                s_variant_ref.async_shutdown(boost::asio::detached);
                                if (logger_copy) logger_copy->trace("[AsyncSessionLC] Initiated async_shutdown for SSL stream (conn_id: {}).", log_cid);
                            }
#else
                            if (logger_copy) logger_copy->warn("[AsyncSessionLC] OpenSSL headers not detected for SSL_get_shutdown check (conn_id: {}). Proceeding with socket shutdown only.", log_cid);
#endif
                        }
                        // Shutdown and close the underlying TCP socket.
                        s_variant_ref.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec_shutdown);
                        if (ec_shutdown && logger_copy) {
                            logger_copy->trace("[AsyncSessionLC] Socket shutdown error (conn_id: {}): {}", log_cid, ec_shutdown.message());
                        }
                        s_variant_ref.lowest_layer().close(ec_close);
                        if (ec_close && logger_copy) {
                            logger_copy->trace("[AsyncSessionLC] Socket close error (conn_id: {}): {}", log_cid, ec_close.message());
                        }
                    }
                },
                stream_context_->stream);
        }

        is_closed_.store(true, std::memory_order_release);  // Mark as fully closed
        stream_context_.reset();                            // Release the stream context ownership

        if (logger) logger->info("[AsyncSessionLC] AsyncSession closed (conn_id was: {}).", conn_id_for_log);
        co_return boltprotocol::BoltError::SUCCESS;
    }

}  // namespace neo4j_bolt_transport