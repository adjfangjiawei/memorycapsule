#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <thread>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

// run_sync_op_with_timeout_tcp 辅助函数保持不变 (来自上次修复)
namespace {
    template <typename AsyncOperation>
    boost::system::error_code run_sync_op_with_timeout_tcp(
        boost::asio::io_context& io_ctx, std::chrono::milliseconds timeout_duration, AsyncOperation op, std::shared_ptr<spdlog::logger> logger, uint64_t conn_id, const std::string& op_name, boost::asio::ip::tcp::socket* socket_to_cancel_on_timeout) {
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
                    if (logger) logger->warn("[ConnAsyncUtilTCP {}] Op '{}' caught system_error: {}", conn_id, op_name, e.what());
                } catch (const std::exception& e) {
                    op_ec_local = boost::asio::error::fault;
                    if (logger) logger->warn("[ConnAsyncUtilTCP {}] Op '{}' caught exception: {}", conn_id, op_name, e.what());
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
                        if (logger) logger->warn("[ConnAsyncUtilTCP {}] Op '{}' timed out.", conn_id, op_name);
                        timed_out_flag = true;
                        result_ec = boost::asio::error::timed_out;
                        if (socket_to_cancel_on_timeout && socket_to_cancel_on_timeout->is_open()) {
                            socket_to_cancel_on_timeout->cancel();
                        }
                        operation_completed_flag.store(true, std::memory_order_release);
                    }
                }
            });
        }

        io_ctx.restart();
        while (!operation_completed_flag.load(std::memory_order_acquire)) {
            if (io_ctx.stopped()) {
                if (logger) logger->warn("[ConnAsyncUtilTCP {}] io_context stopped during op '{}' before completion.", conn_id, op_name);
                if (!timed_out_flag && result_ec == boost::asio::error::would_block) {
                    result_ec = boost::asio::error::interrupted;
                }
                break;
            }
            io_ctx.poll_one();
            if (!operation_completed_flag.load(std::memory_order_acquire) && io_ctx.stopped()) {
                std::this_thread::yield();
                if (io_ctx.stopped()) io_ctx.restart();
            }
        }
        timer.cancel();
        return result_ec;
    }
}  // namespace

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::_stage_tcp_connect() {
            if (plain_iostream_wrapper_) plain_iostream_wrapper_.reset();
            if (ssl_stream_sync_) ssl_stream_sync_.reset();
            if (owned_socket_for_sync_plain_ && owned_socket_for_sync_plain_->is_open()) {
                boost::system::error_code ignored_ec;
                try {
                    owned_socket_for_sync_plain_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
                    owned_socket_for_sync_plain_->close(ignored_ec);
                } catch (...) {
                }
            }
            owned_socket_for_sync_plain_ = std::make_unique<boost::asio::ip::tcp::socket>(io_context_ref_);

            if (logger_) logger_->debug("[ConnTCP {}] Sync TCP Connecting to {}:{} (Timeout: {}ms)", id_, conn_config_.target_host, conn_config_.target_port, conn_config_.tcp_connect_timeout_ms);

            boost::system::error_code ec;
            try {
                boost::asio::ip::tcp::resolver resolver(io_context_ref_);
                boost::asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(conn_config_.target_host, std::to_string(conn_config_.target_port), ec);

                if (ec) {
                    std::string msg = "DNS resolution failed for " + conn_config_.target_host + ": " + ec.message();
                    _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用 internal
                    if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                    return last_error_code_;
                }
                if (endpoints.empty()) {
                    std::string msg = "DNS resolution for " + conn_config_.target_host + " returned no endpoints.";
                    _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用 internal
                    if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                    return last_error_code_;
                }

                auto connect_op_lambda = [&](boost::system::error_code& op_ec_ref) -> boost::asio::awaitable<void> {
                    co_await boost::asio::async_connect(*owned_socket_for_sync_plain_, endpoints, boost::asio::redirect_error(boost::asio::use_awaitable, op_ec_ref));
                    co_return;
                };

                ec = run_sync_op_with_timeout_tcp(io_context_ref_, std::chrono::milliseconds(conn_config_.tcp_connect_timeout_ms), connect_op_lambda, logger_, id_, "Sync TCP Connect", owned_socket_for_sync_plain_.get());

                if (ec) {
                    std::string msg;
                    if (ec == boost::asio::error::timed_out) {
                        msg = "Sync TCP connect to " + conn_config_.target_host + ":" + std::to_string(conn_config_.target_port) + " timed out after " + std::to_string(conn_config_.tcp_connect_timeout_ms) + "ms.";
                    } else {
                        msg = "Sync TCP connect to " + conn_config_.target_host + ":" + std::to_string(conn_config_.target_port) + " failed: " + ec.message();
                    }
                    _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用 internal
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
                        _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, "Socket not open for plain stream wrapper after connect.");  // 使用 internal
                        if (logger_) logger_->error("[ConnTCP {}] Socket not open for plain iostream wrapper post-connect.", id_);
                        return last_error_code_;
                    }
                    try {
                        plain_iostream_wrapper_ = std::make_unique<boost::asio::ip::tcp::iostream>(std::move(*owned_socket_for_sync_plain_));
                        owned_socket_for_sync_plain_.reset();
                        if (!plain_iostream_wrapper_->good()) {
                            std::string msg = "Failed to initialize plain iostream wrapper after TCP connect (stream not good).";
                            _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用 internal
                            if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                            return last_error_code_;
                        }
                    } catch (const std::exception& e) {
                        std::string msg = "Exception creating plain iostream wrapper: " + std::string(e.what());
                        _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用 internal
                        if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                        return last_error_code_;
                    }
                }

            } catch (const boost::system::system_error& e) {
                std::string msg = "ASIO system error during sync TCP connect stage: " + std::string(e.what());
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用 internal
                if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::exception& e) {
                std::string msg = "Standard exception during sync TCP connect stage: " + std::string(e.what());
                _mark_as_defunct_internal(boltprotocol::BoltError::UNKNOWN_ERROR, msg);  // 使用 internal
                if (logger_) logger_->error("[ConnTCP {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::TCP_CONNECTED, std::memory_order_relaxed);
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->debug("[ConnTCP {}] Sync TCP connection established to {}:{}.", id_, conn_config_.target_host, conn_config_.target_port);
            return boltprotocol::BoltError::SUCCESS;
        }

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_stage_tcp_connect_async(boost::asio::ip::tcp::socket& socket, std::chrono::milliseconds timeout) {
            if (logger_) logger_->debug("[ConnTCPAsync {}] Async TCP Connecting to {}:{} (Timeout: {}ms)", get_id_for_logging(), conn_config_.target_host, conn_config_.target_port, timeout.count());

            boost::system::error_code ec;
            boost::asio::ip::tcp::resolver resolver(socket.get_executor());

            auto endpoints = co_await resolver.async_resolve(conn_config_.target_host, std::to_string(conn_config_.target_port), boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            if (ec) {
                std::string msg = "Async DNS resolution failed for " + conn_config_.target_host + ": " + ec.message();
                mark_as_defunct_from_async(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用异步版本
                if (logger_) logger_->error("[ConnTCPAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }
            if (endpoints.empty()) {
                std::string msg = "Async DNS resolution for " + conn_config_.target_host + " returned no endpoints.";
                mark_as_defunct_from_async(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用异步版本
                if (logger_) logger_->error("[ConnTCPAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            boost::asio::steady_timer timer(socket.get_executor());
            bool timed_out = false;

            if (timeout.count() > 0) {
                timer.expires_after(timeout);
                boost::asio::co_spawn(
                    socket.get_executor(),
                    [&]() -> boost::asio::awaitable<void> {
                        boost::system::error_code timer_ec;
                        co_await timer.async_wait(boost::asio::redirect_error(boost::asio::use_awaitable, timer_ec));
                        if (timer_ec != boost::asio::error::operation_aborted) {
                            timed_out = true;
                            socket.cancel();
                            if (logger_) logger_->trace("[ConnTCPAsync {}] Connect op socket cancel called due to timeout.", get_id_for_logging());
                        }
                        co_return;
                    },
                    boost::asio::detached);
            }

            co_await boost::asio::async_connect(socket, endpoints, boost::asio::redirect_error(boost::asio::use_awaitable, ec));

            if (timeout.count() > 0) {
                timer.cancel();
            }

            if (ec) {
                std::string msg;
                if (timed_out || ec == boost::asio::error::operation_aborted) {
                    msg = "Async TCP connect to " + conn_config_.target_host + ":" + std::to_string(conn_config_.target_port) + " timed out or was cancelled.";
                    if (timed_out) ec = boost::asio::error::timed_out;
                } else {
                    msg = "Async TCP connect to " + conn_config_.target_host + ":" + std::to_string(conn_config_.target_port) + " failed: " + ec.message();
                }
                mark_as_defunct_from_async(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用异步版本
                if (logger_) logger_->error("[ConnTCPAsync {}] {}", get_id_for_logging(), msg);
                co_return last_error_code_;
            }

            if (conn_config_.socket_keep_alive_enabled) {
                boost::system::error_code keep_alive_ec;
                socket.set_option(boost::asio::socket_base::keep_alive(true), keep_alive_ec);
                if (keep_alive_ec && logger_) {
                    logger_->warn("[ConnTCPAsync {}] Failed to set SO_KEEPALIVE: {}", get_id_for_logging(), keep_alive_ec.message());
                }
            }
            if (conn_config_.tcp_no_delay_enabled) {
                boost::system::error_code no_delay_ec;
                socket.set_option(boost::asio::ip::tcp::no_delay(true), no_delay_ec);
                if (no_delay_ec && logger_) {
                    logger_->warn("[ConnTCPAsync {}] Failed to set TCP_NODELAY: {}", get_id_for_logging(), no_delay_ec.message());
                }
            }

            current_state_.store(InternalState::TCP_CONNECTED, std::memory_order_relaxed);
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->debug("[ConnTCPAsync {}] Async TCP connection established to {}:{}.", get_id_for_logging(), conn_config_.target_host, conn_config_.target_port);
            co_return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport