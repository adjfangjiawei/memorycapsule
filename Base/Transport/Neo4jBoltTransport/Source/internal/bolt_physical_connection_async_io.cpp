#include <algorithm>
#include <boost/asio/post.hpp>
// #include <boost/asio/read.hpp> // 不再直接需要
// #include <boost/asio/write.hpp>// 不再直接需要
#include <cstring>
#include <iostream>
#include <optional>
#include <variant>
#include <vector>

#include "boltprotocol/detail/byte_order_utils.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/async_utils_decl.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_write_to_active_async_stream(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& stream_variant_ref, const std::vector<uint8_t>& data) {
            if (is_defunct()) {
                if (logger_) logger_->warn("[ConnAsyncIO {}] Async write on defunct connection.", get_id_for_logging());
                co_return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            if (data.empty()) {
                co_return boltprotocol::BoltError::SUCCESS;
            }

            std::chrono::milliseconds timeout(conn_config_.socket_write_timeout_ms);
            if (logger_) logger_->trace("[ConnAsyncIO {}] Async Write {} bytes. Timeout: {}ms", get_id_for_logging(), data.size(), timeout.count());

            std::pair<boltprotocol::BoltError, std::size_t> result;
            result = co_await std::visit(
                [&](auto* stream_ptr) -> boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::size_t>> {
                    if (!stream_ptr) {
                        if (logger_) logger_->error("[ConnAsyncIO {}] Async write: stream_ptr in variant is null.", get_id_for_logging());
                        co_return std::pair<boltprotocol::BoltError, std::size_t>{boltprotocol::BoltError::INVALID_ARGUMENT, 0};
                    }
                    // 调用 async_write_with_timeout
                    co_return co_await async_utils::async_write_with_timeout(this, *stream_ptr, boost::asio::buffer(data), timeout, "Async Write");
                },
                stream_variant_ref);

            if (result.first != boltprotocol::BoltError::SUCCESS) {
                co_return get_last_error_code_from_async();
            }
            if (result.second != data.size()) {
                std::string msg = "Partial async write. Expected " + std::to_string(data.size()) + ", wrote " + std::to_string(result.second);
                mark_as_defunct_from_async(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnAsyncIO {}] {}", get_id_for_logging(), msg);
                co_return get_last_error_code_from_async();
            }
            co_return boltprotocol::BoltError::SUCCESS;
        }

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> BoltPhysicalConnection::_read_from_active_async_stream(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& stream_variant_ref, size_t size_to_read) {
            if (is_defunct()) {
                if (logger_) logger_->warn("[ConnAsyncIO {}] Async read on defunct connection.", get_id_for_logging());
                co_return std::pair<boltprotocol::BoltError, std::vector<uint8_t>>{last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR, {}};
            }
            if (size_to_read == 0) {
                co_return std::pair<boltprotocol::BoltError, std::vector<uint8_t>>{boltprotocol::BoltError::SUCCESS, {}};
            }

            std::vector<uint8_t> buffer_vec(size_to_read);
            std::chrono::milliseconds timeout(conn_config_.socket_read_timeout_ms);
            if (logger_) logger_->trace("[ConnAsyncIO {}] Async Read {} bytes. Timeout: {}ms", get_id_for_logging(), size_to_read, timeout.count());

            std::pair<boltprotocol::BoltError, std::size_t> result;
            result = co_await std::visit(
                [&](auto* stream_ptr) -> boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::size_t>> {
                    if (!stream_ptr) {
                        if (logger_) logger_->error("[ConnAsyncIO {}] Async read: stream_ptr in variant is null.", get_id_for_logging());
                        co_return std::pair<boltprotocol::BoltError, std::size_t>{boltprotocol::BoltError::INVALID_ARGUMENT, 0};
                    }
                    // 调用 async_read_with_timeout
                    co_return co_await async_utils::async_read_with_timeout(this, *stream_ptr, boost::asio::buffer(buffer_vec), timeout, "Async Read");
                },
                stream_variant_ref);

            if (result.first != boltprotocol::BoltError::SUCCESS) {
                co_return std::pair<boltprotocol::BoltError, std::vector<uint8_t>>{get_last_error_code_from_async(), {}};
            }
            if (result.second < size_to_read) {
                std::string msg = "Incomplete async read. Expected " + std::to_string(size_to_read) + ", got " + std::to_string(result.second);
                mark_as_defunct_from_async(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnAsyncIO {}] {}", get_id_for_logging(), msg);
                co_return std::pair<boltprotocol::BoltError, std::vector<uint8_t>>{get_last_error_code_from_async(), {}};
            }
            co_return std::pair<boltprotocol::BoltError, std::vector<uint8_t>>{boltprotocol::BoltError::SUCCESS, std::move(buffer_vec)};
        }
    }  // namespace internal
}  // namespace neo4j_bolt_transport