#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>  // For boost::asio::async_connect with endpoint sequence
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <iostream>
#include <thread>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // Helper function (run_with_timeout_sync_internal) remains the same as in the previous response.
        // Ensure it's either here or in a common utility file and correctly included.
        // For brevity, I'm not repeating it here but assuming it's available.
        // ... (run_with_timeout_sync_internal definition from previous response) ...
        template <typename AsyncOperation>
        boost::system::error_code run_with_timeout_sync_internal(
            boost::asio::io_context& io_ctx, std::chrono::milliseconds timeout_duration, AsyncOperation op, std::shared_ptr<spdlog::logger> logger, uint64_t conn_id, const std::string& op_name, boost::asio::ip::tcp::socket* socket_to_cancel_on_timeout = nullptr) {
            boost::system::error_code result_ec = boost::asio::error::would_block;
            std::atomic<bool> operation_completed_flag{false};

            boost::asio::co_spawn(
                io_ctx,
                [&]() -> boost::asio::awaitable<void> {
                    boost::system::error_code op_ec_local;
                    try {
                        co_await op(op_ec_local);
                    } catch (const boost::system::system_error& e) {
                        op_ec_local = e.code();
                        if (logger) logger->warn("[ConnAsyncUtil {}] Op '{}' caught system_error: {}", conn_id, op_name, e.what());
                    } catch (const std::exception& e) {
                        op_ec_local = boost::asio::error::fault;
                        if (logger) logger->warn("[ConnAsyncUtil {}] Op '{}' caught exception: {}", conn_id, op_name, e.what());
                    }
                    result_ec = op_ec_local;
                    operation_completed_flag.store(true, std::memory_order_release);
                    co_return;
                },
                boost::asio::detached);

            boost::asio::steady_timer timer(io_ctx);
            bool timed_out_flag = false;

            if (timeout_duration.count() > 0) {
                timer.expires_after(timeout_duration);
                timer.async_wait([&, logger, conn_id, op_name, socket_to_cancel_on_timeout](const boost::system::error_code& ec_timer) {
                    if (ec_timer != boost::asio::error::operation_aborted) {
                        if (!operation_completed_flag.load(std::memory_order_acquire)) {
                            if (logger) logger->warn("[ConnAsyncUtil {}] Op '{}' timed out.", conn_id, op_name);
                            timed_out_flag = true;
                            result_ec = boost::asio::error::timed_out;
                            if (socket_to_cancel_on_timeout && socket_to_cancel_on_timeout->is_open()) {
                                boost::system::error_code cancel_ec;
                                socket_to_cancel_on_timeout->cancel(cancel_ec);
                                if (logger && cancel_ec) logger->warn("[ConnAsyncUtil {}] Op '{}' timeout socket cancel error: {}", conn_id, op_name, cancel_ec.message());
                            }
                            operation_completed_flag.store(true, std::memory_order_release);
                        }
                    }
                });
            }

            io_ctx.restart();
            while (!operation_completed_flag.load(std::memory_order_acquire)) {
                if (io_ctx.stopped()) {
                    if (logger) logger->warn("[ConnAsyncUtil {}] io_context stopped during op '{}' before completion.", conn_id, op_name);
                    if (!timed_out_flag && result_ec == boost::asio::error::would_block) {
                        result_ec = boost::asio::error::interrupted;
                    }
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

        boltprotocol::BoltError BoltPhysicalConnection::_stage_tcp_connect() {
            // ... (resource reset logic remains the same) ...
            if (plain_iostream_wrapper_) plain_iostream_wrapper_.reset();
            if (ssl_stream_sync_) ssl_stream_sync_.reset();
            if (owned_socket_for_sync_plain_ && owned_socket_for_sync_plain_->is_open()) {
                boost::system::error_code ignored_ec;
                try {
                    owned_socket_for_sync_plain_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
                    owned_socket_for_sync_plain_->close(ignored_ec);
                } catch (...) { /* ignore */
                }
            }
            owned_socket_for_sync_plain_.reset();
            if (ssl_context_sync_) ssl_context_sync_.reset();

            current_state_.store(InternalState::TCP_CONNECTING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnTCP {}] TCP Connecting to {}:{} (Timeout: {}ms)", id_, conn_config_.target_host, conn_config_.target_port, conn_config_.tcp_connect_timeout_ms);

            owned_socket_for_sync_plain_ = std::make_unique<boost::asio::ip::tcp::socket>(io_context_ref_);
            boost::system::error_code ec;

            try {
                boost::asio::ip::tcp::resolver resolver(io_context_ref_);
                boost::asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(conn_config_.target_host, std::to_string(conn_config_.target_port), ec);

                if (ec) {
                    std::string msg = "DNS resolution failed for " + conn_config_.target_host + ": " + ec.message();
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                    if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                    return last_error_code_;
                }
                if (endpoints.empty()) {
                    std::string msg = "DNS resolution for " + conn_config_.target_host + " returned no endpoints.";
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                    if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                    return last_error_code_;
                }

                auto connect_op = [&](boost::system::error_code& op_ec_ref) -> boost::asio::awaitable<void> {
                    try {
                        // Use boost::asio::async_connect (free function) for connecting to a sequence of endpoints
                        co_await boost::asio::async_connect(*owned_socket_for_sync_plain_, endpoints, boost::asio::redirect_error(boost::asio::use_awaitable, op_ec_ref));
                    } catch (const boost::system::system_error& e) {
                        op_ec_ref = e.code();
                    }
                    co_return;
                };

                ec = run_with_timeout_sync_internal(io_context_ref_, std::chrono::milliseconds(conn_config_.tcp_connect_timeout_ms), connect_op, logger_, id_, "TCP Connect", owned_socket_for_sync_plain_.get());

                // ... (rest of the _stage_tcp_connect method, error handling, options, iostream wrapper logic remains the same) ...
                if (ec) {
                    std::string msg;
                    if (ec == boost::asio::error::timed_out) {
                        msg = "TCP connect to " + conn_config_.target_host + ":" + std::to_string(conn_config_.target_port) + " timed out after " + std::to_string(conn_config_.tcp_connect_timeout_ms) + "ms.";
                    } else {
                        msg = "TCP connect to " + conn_config_.target_host + ":" + std::to_string(conn_config_.target_port) + " failed: " + ec.message();
                    }
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                    if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                    return last_error_code_;
                }

                if (conn_config_.socket_keep_alive_enabled) {
                    boost::system::error_code keep_alive_ec;
                    owned_socket_for_sync_plain_->set_option(boost::asio::socket_base::keep_alive(true), keep_alive_ec);
                    if (keep_alive_ec && logger_) {
                        logger_->warn("[ConnTCP {}] Failed to set SO_KEEPALIVE: {}", id_, keep_alive_ec.message());
                    }
                }
                if (conn_config_.tcp_no_delay_enabled) {
                    boost::system::error_code no_delay_ec;
                    owned_socket_for_sync_plain_->set_option(boost::asio::ip::tcp::no_delay(true), no_delay_ec);
                    if (no_delay_ec && logger_) {
                        logger_->warn("[ConnTCP {}] Failed to set TCP_NODELAY: {}", id_, no_delay_ec.message());
                    }
                }

                if (!conn_config_.encryption_enabled) {
                    if (!owned_socket_for_sync_plain_ || !owned_socket_for_sync_plain_->is_open()) {
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "Socket not open for plain stream wrapper.");
                        if (logger_) logger_->error("[ConnTCP {}] Socket not open for plain iostream wrapper after successful connect.", id_);
                        return last_error_code_;
                    }
                    try {
                        plain_iostream_wrapper_ = std::make_unique<boost::asio::ip::tcp::iostream>(std::move(*owned_socket_for_sync_plain_));
                        owned_socket_for_sync_plain_.reset();
                        if (!plain_iostream_wrapper_->good()) {
                            std::string msg = "Failed to initialize plain iostream wrapper after TCP connect.";
                            _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                            if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                            return last_error_code_;
                        }
                    } catch (const std::exception& e) {
                        std::string msg = "Exception creating plain iostream wrapper: " + std::string(e.what());
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                        if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                        return last_error_code_;
                    }
                }

            } catch (const boost::system::system_error& e) {
                std::string msg = "ASIO system error during TCP connect stage: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::exception& e) {
                std::string msg = "Standard exception during TCP connect stage: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::TCP_CONNECTED, std::memory_order_relaxed);
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->debug("[ConnTCP {}] TCP connection established to {}:{}.", id_, conn_config_.target_host, conn_config_.target_port);
            return boltprotocol::BoltError::SUCCESS;
        }

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_stage_tcp_connect_async(boost::asio::ip::tcp::socket& socket_ref, std::chrono::milliseconds timeout) {
            if (logger_) logger_->debug("[ConnTCPAsync {}] TCP Connecting async to {}:{} (Timeout: {}ms)", id_, conn_config_.target_host, conn_config_.target_port, timeout.count());
            boost::system::error_code ec;

            boost::asio::ip::tcp::resolver resolver(co_await boost::asio::this_coro::executor);
            boost::asio::ip::tcp::resolver::results_type endpoints = co_await resolver.async_resolve(conn_config_.target_host, std::to_string(conn_config_.target_port), boost::asio::redirect_error(boost::asio::use_awaitable, ec));

            if (ec) {
                std::string msg = "Async DNS resolution failed: " + ec.message();
                if (logger_) logger_->error("[ConnTCPAsync {}] {}", id_, msg);
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                co_return boltprotocol::BoltError::NETWORK_ERROR;
            }
            if (endpoints.empty()) {
                std::string msg = "Async DNS resolution returned no endpoints for " + conn_config_.target_host;
                if (logger_) logger_->error("[ConnTCPAsync {}] {}", id_, msg);
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                co_return boltprotocol::BoltError::NETWORK_ERROR;
            }

            boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor);
            std::atomic<bool> connect_timed_out{false};
            std::atomic<bool> connect_completed{false};

            if (timeout.count() > 0) {
                timer.expires_after(timeout);
                boost::asio::co_spawn(
                    co_await boost::asio::this_coro::executor,
                    [&]() -> boost::asio::awaitable<void> {
                        boost::system::error_code timer_ec;
                        co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, timer_ec));
                        if (timer_ec != boost::asio::error::operation_aborted) {
                            if (!connect_completed.load(std::memory_order_acquire)) {
                                connect_timed_out.store(true, std::memory_order_release);
                                boost::system::error_code cancel_ec;
                                socket_ref.cancel(cancel_ec);
                                if (logger_) logger_->warn("[ConnTCPAsync {}] TCP connect async timed out, socket cancelled (ec: {}).", id_, cancel_ec.message());
                            }
                        }
                        co_return;
                    },
                    boost::asio::detached);
            }

            // Use boost::asio::async_connect (free function) for EndpointSequence
            co_await boost::asio::async_connect(socket_ref, endpoints, boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            connect_completed.store(true, std::memory_order_release);
            if (timeout.count() > 0) {
                timer.cancel();
            }

            if (ec) {
                std::string msg;
                if (connect_timed_out.load(std::memory_order_acquire) || ec == boost::asio::error::operation_aborted) {
                    msg = "Async TCP connect to " + conn_config_.target_host + ":" + std::to_string(conn_config_.target_port) + " timed out or was cancelled.";
                    ec = boost::asio::error::timed_out;
                } else {
                    msg = "Async TCP connect failed: " + ec.message();
                }
                if (logger_) logger_->error("[ConnTCPAsync {}] {}", id_, msg);
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                co_return boltprotocol::BoltError::NETWORK_ERROR;
            }

            // ... (socket options setting remains the same) ...
            if (conn_config_.socket_keep_alive_enabled) {
                boost::system::error_code keep_alive_ec;
                socket_ref.set_option(boost::asio::socket_base::keep_alive(true), keep_alive_ec);
                if (keep_alive_ec && logger_) logger_->warn("[ConnTCPAsync {}] Failed to set SO_KEEPALIVE (async): {}", id_, keep_alive_ec.message());
            }
            if (conn_config_.tcp_no_delay_enabled) {
                boost::system::error_code no_delay_ec;
                socket_ref.set_option(boost::asio::ip::tcp::no_delay(true), no_delay_ec);
                if (no_delay_ec && logger_) logger_->warn("[ConnTCPAsync {}] Failed to set TCP_NODELAY (async): {}", id_, no_delay_ec.message());
            }

            if (logger_) logger_->debug("[ConnTCPAsync {}] Async TCP connection established.", id_);
            current_state_.store(InternalState::TCP_CONNECTED);
            co_return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport