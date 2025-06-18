#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_UTILS_IMPL_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_UTILS_IMPL_H

#include <spdlog/spdlog.h>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <optional>
#include <tuple>
#include <type_traits>  // For std::invoke_result_t

#include "boltprotocol/bolt_errors_versions.h"
#include "neo4j_bolt_transport/internal/i_async_context_callbacks.h"

namespace neo4j_bolt_transport {
    namespace internal {
        namespace async_utils {

            // Internal helper for timed I/O operations that return awaitable<tuple<ec, size_t>>
            template <typename Stream, typename IoOperation>
            boost::asio::awaitable<std::tuple<boost::system::error_code, std::size_t>> perform_timed_io(IAsyncContextCallbacks* callbacks,
                                                                                                        Stream& stream,
                                                                                                        std::chrono::milliseconds timeout_duration,
                                                                                                        const std::string& operation_name_for_log,
                                                                                                        IoOperation io_op  // Lambda returning awaitable<tuple<ec, size_t>>
            ) {
                std::shared_ptr<spdlog::logger> logger = nullptr;
                if (callbacks) {
                    logger = callbacks->get_logger();
                }

                if (!stream.lowest_layer().is_open()) {
                    if (logger) logger->error("[AsyncUtilTimed {}] Op on closed stream (obj id {}).", operation_name_for_log, callbacks ? callbacks->get_id_for_logging() : 0);
                    co_return std::make_tuple(boost::asio::error::not_connected, 0);  // Or appropriate error
                }

                if (timeout_duration.count() <= 0) {  // No timeout
                    try {
                        co_return co_await io_op();
                    } catch (const boost::system::system_error& e_sys) {
                        if (logger) logger->error("[AsyncUtilTimed {}] Exception (no timeout, obj id {}): {}", operation_name_for_log, callbacks ? callbacks->get_id_for_logging() : 0, e_sys.what());
                        co_return std::make_tuple(e_sys.code(), 0);
                    }
                }

                boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor);
                timer.expires_after(timeout_duration);

                std::optional<std::tuple<boost::system::error_code, std::size_t>> io_result_opt;
                std::optional<boost::system::error_code> timer_result_opt;

                boost::asio::co_spawn(
                    co_await boost::asio::this_coro::executor,
                    [&]() -> boost::asio::awaitable<void> {
                        std::tuple<boost::system::error_code, std::size_t> temp_io_res;
                        try {
                            temp_io_res = co_await io_op();
                        } catch (const boost::system::system_error& e_sys) {
                            std::get<0>(temp_io_res) = e_sys.code();
                            std::get<1>(temp_io_res) = 0;
                        }
                        if (!timer_result_opt.has_value()) {
                            io_result_opt = temp_io_res;
                            timer.cancel();
                        }
                        co_return;
                    },
                    boost::asio::detached);

                boost::asio::co_spawn(
                    co_await boost::asio::this_coro::executor,
                    [&]() -> boost::asio::awaitable<void> {
                        boost::system::error_code timer_ec_val;
                        std::tie(timer_ec_val) = co_await timer.async_wait(boost::asio::as_tuple(boost::asio::use_awaitable));
                        if (!io_result_opt.has_value()) {
                            timer_result_opt = {timer_ec_val};
                            if (timer_ec_val != boost::asio::error::operation_aborted) {
                                stream.lowest_layer().cancel();
                            }
                        }
                        co_return;
                    },
                    boost::asio::detached);

                while (!io_result_opt.has_value() && !timer_result_opt.has_value()) {
                    co_await boost::asio::post(co_await boost::asio::this_coro::executor, boost::asio::use_awaitable);
                }

                if (io_result_opt.has_value()) {
                    co_return io_result_opt.value();
                } else if (timer_result_opt.has_value() && timer_result_opt.value() != boost::asio::error::operation_aborted) {
                    if (logger) logger->warn("[AsyncUtilTimed {}] Op timed out (obj id {}).", operation_name_for_log, callbacks ? callbacks->get_id_for_logging() : 0);
                    co_return std::make_tuple(boost::asio::error::timed_out, 0);
                } else if (timer_result_opt.has_value() && timer_result_opt.value() == boost::asio::error::operation_aborted) {
                    if (!io_result_opt.has_value()) {
                        if (logger) logger->error("[AsyncUtilTimed {}] Logic error: Timer aborted, no I/O (obj id {}).", operation_name_for_log, callbacks ? callbacks->get_id_for_logging() : 0);
                        co_return std::make_tuple(boost::asio::error::fault, 0);
                    }
                    // This should be caught by the first if (io_result_opt.has_value())
                    if (logger) logger->error("[AsyncUtilTimed {}] Logic error: Timer aborted, but io_result_opt is somehow still empty (obj id {}).", operation_name_for_log, callbacks ? callbacks->get_id_for_logging() : 0);
                    co_return std::make_tuple(boost::asio::error::fault, 0);
                } else {
                    if (logger) logger->error("[AsyncUtilTimed {}] Unexpected fallthrough (obj id {}).", operation_name_for_log, callbacks ? callbacks->get_id_for_logging() : 0);
                    co_return std::make_tuple(boost::asio::error::fault, 0);
                }
            }

            template <typename Stream, typename MutableBufferSequence>
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::size_t>> async_read_with_timeout(IAsyncContextCallbacks* callbacks, Stream& stream, MutableBufferSequence buffers, std::chrono::milliseconds timeout_duration, const std::string& operation_name) {
                static_assert(boost::asio::is_mutable_buffer_sequence<MutableBufferSequence>::value, "async_read_with_timeout requires a MutableBufferSequence.");

                auto completion_token = boost::asio::as_tuple(boost::asio::use_awaitable);
                auto io_op_lambda = [&]() {  // Lambda for the actual read operation
                    return boost::asio::async_read(stream, buffers, completion_token);
                };

                auto [ec, bytes_transferred] = co_await perform_timed_io(callbacks, stream, timeout_duration, operation_name, io_op_lambda);

                if (ec) {
                    boltprotocol::BoltError mapped_error = boltprotocol::BoltError::NETWORK_ERROR;
                    if (ec == boost::asio::error::eof)
                        mapped_error = boltprotocol::BoltError::NETWORK_ERROR;
                    else if (ec == boost::asio::error::timed_out)
                        mapped_error = boltprotocol::BoltError::NETWORK_ERROR;
                    else if (ec == boost::asio::error::invalid_argument)
                        mapped_error = boltprotocol::BoltError::INVALID_ARGUMENT;
                    else if (ec == boost::asio::error::not_connected)
                        mapped_error = boltprotocol::BoltError::NETWORK_ERROR;
                    else if (ec == boost::asio::error::fault)
                        mapped_error = boltprotocol::BoltError::UNKNOWN_ERROR;

                    if (callbacks) {  // callbacks might be null if initial check failed
                        callbacks->mark_as_defunct_from_async(mapped_error, operation_name + " failed: " + ec.message());
                        co_return std::pair<boltprotocol::BoltError, std::size_t>{callbacks->get_last_error_code_from_async(), 0};
                    } else {
                        co_return std::pair<boltprotocol::BoltError, std::size_t>{mapped_error, 0};
                    }
                }
                co_return std::pair<boltprotocol::BoltError, std::size_t>{boltprotocol::BoltError::SUCCESS, bytes_transferred};
            }

            template <typename Stream, typename ConstBufferSequence>
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::size_t>> async_write_with_timeout(IAsyncContextCallbacks* callbacks, Stream& stream, ConstBufferSequence buffers, std::chrono::milliseconds timeout_duration, const std::string& operation_name) {
                static_assert(boost::asio::is_const_buffer_sequence<ConstBufferSequence>::value, "async_write_with_timeout requires a ConstBufferSequence.");

                auto completion_token = boost::asio::as_tuple(boost::asio::use_awaitable);
                auto io_op_lambda = [&]() {  // Lambda for the actual write operation
                    return boost::asio::async_write(stream, buffers, completion_token);
                };

                auto [ec, bytes_transferred] = co_await perform_timed_io(callbacks, stream, timeout_duration, operation_name, io_op_lambda);

                if (ec) {
                    boltprotocol::BoltError mapped_error = boltprotocol::BoltError::NETWORK_ERROR;
                    if (ec == boost::asio::error::eof)
                        mapped_error = boltprotocol::BoltError::NETWORK_ERROR;
                    else if (ec == boost::asio::error::timed_out)
                        mapped_error = boltprotocol::BoltError::NETWORK_ERROR;
                    else if (ec == boost::asio::error::invalid_argument)
                        mapped_error = boltprotocol::BoltError::INVALID_ARGUMENT;
                    else if (ec == boost::asio::error::not_connected)
                        mapped_error = boltprotocol::BoltError::NETWORK_ERROR;
                    else if (ec == boost::asio::error::fault)
                        mapped_error = boltprotocol::BoltError::UNKNOWN_ERROR;

                    if (callbacks) {
                        callbacks->mark_as_defunct_from_async(mapped_error, operation_name + " failed: " + ec.message());
                        co_return std::pair<boltprotocol::BoltError, std::size_t>{callbacks->get_last_error_code_from_async(), 0};
                    } else {
                        co_return std::pair<boltprotocol::BoltError, std::size_t>{mapped_error, 0};
                    }
                }
                co_return std::pair<boltprotocol::BoltError, std::size_t>{boltprotocol::BoltError::SUCCESS, bytes_transferred};
            }

        }  // namespace async_utils
    }  // namespace internal
}  // namespace neo4j_bolt_transport

#endif