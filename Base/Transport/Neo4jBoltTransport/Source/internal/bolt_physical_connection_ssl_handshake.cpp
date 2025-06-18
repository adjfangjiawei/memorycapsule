#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>  // For host_name_verification
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <iostream>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

// For OpenSSL specific calls if needed (SNI, error details)
#include <openssl/err.h>  // For ERR_get_error, ERR_error_string_n
#include <openssl/ssl.h>  // For SSL_set_tlsext_host_name

namespace neo4j_bolt_transport {
    namespace internal {

        // Re-declare or include the helper if it's made common
        template <typename AsyncOperation>
        boost::system::error_code run_with_timeout_sync_internal_hs(  // Renamed to avoid conflict if it stays local
            boost::asio::io_context& io_ctx,
            std::chrono::milliseconds timeout_duration,
            AsyncOperation op,
            std::shared_ptr<spdlog::logger> logger,
            uint64_t conn_id,
            const std::string& op_name,
            boost::asio::ssl::stream<boost::asio::ip::tcp::socket>* stream_to_cancel_on_timeout = nullptr) {
            boost::system::error_code result_ec = boost::asio::error::would_block;
            std::atomic<bool> operation_completed_flag{false};

            boost::asio::co_spawn(
                io_ctx,
                [&]() -> boost::asio::awaitable<void> {
                    boost::system::error_code op_ec;
                    try {
                        co_await op(op_ec);
                    } catch (const boost::system::system_error& e) {
                        op_ec = e.code();
                        if (logger) logger->warn("[ConnAsyncUtilHS {}] Op '{}' caught system_error: {}", conn_id, op_name, e.what());
                    } catch (const std::exception& e) {
                        op_ec = boost::asio::error::fault;
                        if (logger) logger->warn("[ConnAsyncUtilHS {}] Op '{}' caught exception: {}", conn_id, op_name, e.what());
                    }
                    result_ec = op_ec;
                    operation_completed_flag.store(true, std::memory_order_release);
                    co_return;
                },
                boost::asio::detached);

            boost::asio::steady_timer timer(io_ctx);
            bool timed_out_flag = false;

            if (timeout_duration.count() > 0) {
                timer.expires_after(timeout_duration);
                timer.async_wait([&](const boost::system::error_code& ec_timer) {
                    if (ec_timer != boost::asio::error::operation_aborted) {
                        if (!operation_completed_flag.load(std::memory_order_acquire)) {
                            if (logger) logger->warn("[ConnAsyncUtilHS {}] Op '{}' timed out.", conn_id, op_name);
                            timed_out_flag = true;
                            result_ec = boost::asio::error::timed_out;
                            if (stream_to_cancel_on_timeout && stream_to_cancel_on_timeout->lowest_layer().is_open()) {
                                boost::system::error_code cancel_ec;
                                // For SSL stream, cancelling the lowest layer is usually the way for handshake
                                stream_to_cancel_on_timeout->lowest_layer().cancel(cancel_ec);
                                if (logger && cancel_ec) logger->warn("[ConnAsyncUtilHS {}] Op '{}' timeout socket cancel error: {}", conn_id, op_name, cancel_ec.message());
                            }
                            operation_completed_flag.store(true, std::memory_order_release);
                        }
                    }
                });
            }

            io_ctx.restart();
            while (!operation_completed_flag.load(std::memory_order_acquire)) {
                if (io_ctx.stopped()) {
                    if (logger) logger->warn("[ConnAsyncUtilHS {}] io_context stopped during op '{}'.", conn_id, op_name);
                    if (!timed_out_flag) result_ec = boost::asio::error::interrupted;
                    break;
                }
                io_ctx.poll_one();
                if (!operation_completed_flag.load(std::memory_order_acquire) && io_ctx.stopped()) {
                    std::this_thread::yield();
                }
            }
            timer.cancel();
            return result_ec;
        }

        boltprotocol::BoltError BoltPhysicalConnection::_stage_ssl_handshake() {
            if (!conn_config_.encryption_enabled) {
                // This stage is skipped if encryption is not enabled.
                if (logger_) logger_->debug("[ConnSSLHS {}] SSL encryption not enabled, skipping handshake.", id_);
                return boltprotocol::BoltError::SUCCESS;
            }
            if (current_state_.load(std::memory_order_relaxed) != InternalState::SSL_CONTEXT_SETUP) {
                std::string msg = "SSL handshake called in unexpected state: " + _get_current_state_as_string() + ". Expected SSL_CONTEXT_SETUP.";
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnSSLHS {}] {}", id_, msg);
                return last_error_code_;
            }
            // owned_socket_for_sync_plain_ should have been connected in _stage_tcp_connect
            // and ssl_context_sync_ should have been set up in _stage_ssl_context_setup.
            if (!ssl_context_sync_ || !owned_socket_for_sync_plain_ || !owned_socket_for_sync_plain_->is_open()) {
                std::string msg = "SSL handshake attempted without a valid SSL context or a connected TCP socket.";
                _mark_as_defunct(boltprotocol::BoltError::INVALID_ARGUMENT, msg);
                if (logger_) logger_->error("[ConnSSLHS {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::SSL_HANDSHAKING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnSSLHS {}] Performing SSL handshake for host {} (Timeout: {}ms)...", id_, conn_config_.target_host, conn_config_.bolt_handshake_timeout_ms);

            try {
                // Create the SSL stream, taking ownership of the socket
                ssl_stream_sync_ = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(std::move(*owned_socket_for_sync_plain_), *ssl_context_sync_);
                owned_socket_for_sync_plain_.reset();  // Socket ownership transferred

                // Set SNI (Server Name Indication) and hostname verification
                if (conn_config_.hostname_verification_enabled && conn_config_.resolved_encryption_strategy != config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS) {
                    // Set SNI hostname for TLS extension
                    if (!SSL_set_tlsext_host_name(ssl_stream_sync_->native_handle(), conn_config_.target_host.c_str())) {
                        boost::system::error_code sni_ec(static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category());
                        // SNI failure is usually not fatal for the handshake itself but might lead to cert validation issues.
                        if (logger_) logger_->warn("[ConnSSLHS {}] Failed to set SNI extension for host {}: {}. Handshake will proceed.", id_, conn_config_.target_host, sni_ec.message());
                    } else {
                        if (logger_) logger_->trace("[ConnSSLHS {}] SNI hostname set to: {}", id_, conn_config_.target_host);
                    }

                    // Set the verify callback for hostname verification.
                    ssl_stream_sync_->set_verify_callback(boost::asio::ssl::host_name_verification(conn_config_.target_host));
                    if (logger_) logger_->trace("[ConnSSLHS {}] Hostname verification enabled for: {}", id_, conn_config_.target_host);
                } else {
                    if (logger_) logger_->debug("[ConnSSLHS {}] Hostname verification skipped (disabled or trust_all_certs).", id_);
                }

                // Perform the SSL handshake
                boost::system::error_code handshake_ec;
                auto handshake_op = [&](boost::system::error_code& op_ec_ref) -> boost::asio::awaitable<void> {
                    try {
                        co_await ssl_stream_sync_->async_handshake(boost::asio::ssl::stream_base::client, boost::asio::redirect_error(boost::asio::use_awaitable, op_ec_ref));
                    } catch (const boost::system::system_error& e) {
                        op_ec_ref = e.code();
                    }
                    co_return;
                };

                handshake_ec = run_with_timeout_sync_internal_hs(io_context_ref_,
                                                                 std::chrono::milliseconds(conn_config_.bolt_handshake_timeout_ms),  // Using bolt_handshake_timeout_ms
                                                                 handshake_op,
                                                                 logger_,
                                                                 id_,
                                                                 "SSL Handshake",
                                                                 ssl_stream_sync_.get());

                if (handshake_ec) {
                    std::string msg;
                    if (handshake_ec == boost::asio::error::timed_out) {
                        msg = "SSL handshake timed out for host " + conn_config_.target_host + " after " + std::to_string(conn_config_.bolt_handshake_timeout_ms) + "ms.";
                    } else {
                        msg = "SSL handshake failed for host " + conn_config_.target_host + ": " + handshake_ec.message();
                        // Append OpenSSL error details if available
                        unsigned long openssl_err_code = ERR_get_error();  // Get last OpenSSL error
                        while (openssl_err_code != 0) {                    // Loop if there are multiple errors in the queue
                            char err_buf[256];
                            ERR_error_string_n(openssl_err_code, err_buf, sizeof(err_buf));
                            msg += " (OpenSSL: " + std::string(err_buf) + ")";
                            openssl_err_code = ERR_get_error();  // Get next error
                        }
                    }
                    _mark_as_defunct(boltprotocol::BoltError::HANDSHAKE_FAILED, msg);
                    if (logger_) logger_->error("[ConnSSLHS {}] {}", id_, msg);
                    return last_error_code_;
                }

            } catch (const boost::system::system_error& e) {
                std::string msg = "ASIO system error during SSL handshake: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::HANDSHAKE_FAILED, msg);
                if (logger_) logger_->error("[ConnSSLHS {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::exception& e) {
                std::string msg = "Standard exception during SSL handshake: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnSSLHS {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::SSL_HANDSHAKEN, std::memory_order_relaxed);
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->debug("[ConnSSLHS {}] SSL handshake successful for {}.", id_, conn_config_.target_host);
            return boltprotocol::BoltError::SUCCESS;
        }

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_stage_ssl_handshake_async(boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>& stream_ref,  // Pass stream by reference
                                                                                                           std::chrono::milliseconds timeout) {
            if (logger_) logger_->debug("[ConnSSLHSAsync {}] Performing SSL handshake async for host {} (Timeout: {}ms)...", id_, conn_config_.target_host, timeout.count());
            boost::system::error_code ec;

            // SNI and hostname verification setup
            if (conn_config_.hostname_verification_enabled && conn_config_.resolved_encryption_strategy != config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS) {
                if (!SSL_set_tlsext_host_name(stream_ref.native_handle(), conn_config_.target_host.c_str())) {
                    if (logger_) logger_->warn("[ConnSSLHSAsync {}] Failed to set SNI (async) for host {}", id_, conn_config_.target_host);
                }
                stream_ref.set_verify_callback(boost::asio::ssl::host_name_verification(conn_config_.target_host));
            }

            boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor);
            std::atomic<bool> handshake_timed_out{false};
            std::atomic<bool> handshake_completed{false};

            if (timeout.count() > 0) {
                timer.expires_after(timeout);
                boost::asio::co_spawn(
                    co_await boost::asio::this_coro::executor,
                    [&]() -> boost::asio::awaitable<void> {
                        boost::system::error_code timer_ec;
                        co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, timer_ec));
                        if (timer_ec != boost::asio::error::operation_aborted) {
                            if (!handshake_completed.load(std::memory_order_acquire)) {
                                handshake_timed_out.store(true, std::memory_order_release);
                                boost::system::error_code cancel_ec;
                                stream_ref.lowest_layer().cancel(cancel_ec);  // Cancel underlying socket
                                if (logger_) logger_->warn("[ConnSSLHSAsync {}] SSL handshake async timed out, lowest_layer cancelled (ec: {}).", id_, cancel_ec.message());
                            }
                        }
                        co_return;
                    },
                    boost::asio::detached);
            }

            // Perform handshake
            co_await stream_ref.async_handshake(boost::asio::ssl::stream_base::client, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            handshake_completed.store(true, std::memory_order_release);
            if (timeout.count() > 0) {
                timer.cancel();
            }

            if (ec) {
                std::string msg;
                if (handshake_timed_out.load(std::memory_order_acquire) || ec == boost::asio::error::operation_aborted) {
                    msg = "Async SSL handshake for host " + conn_config_.target_host + " timed out or was cancelled.";
                    ec = boost::asio::error::timed_out;
                } else {
                    msg = "Async SSL handshake failed: " + ec.message();
                    unsigned long openssl_err_code = ERR_get_error();
                    while (openssl_err_code != 0) {
                        char err_buf[256];
                        ERR_error_string_n(openssl_err_code, err_buf, sizeof(err_buf));
                        msg += " (OpenSSL: " + std::string(err_buf) + ")";
                        openssl_err_code = ERR_get_error();
                    }
                }
                if (logger_) logger_->error("[ConnSSLHSAsync {}] {}", id_, msg);
                _mark_as_defunct(boltprotocol::BoltError::HANDSHAKE_FAILED, msg);
                co_return boltprotocol::BoltError::HANDSHAKE_FAILED;
            }

            if (logger_) logger_->debug("[ConnSSLHSAsync {}] Async SSL handshake successful.", id_);
            current_state_.store(InternalState::SSL_HANDSHAKEN);
            co_return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport