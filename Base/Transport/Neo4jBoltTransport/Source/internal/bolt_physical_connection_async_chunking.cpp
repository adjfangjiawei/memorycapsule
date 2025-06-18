#include <algorithm>  // For std::min
#include <cstring>    // For std::memcpy
#include <variant>
#include <vector>

#include "boltprotocol/detail/byte_order_utils.h"
#include "boltprotocol/message_defs.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_send_chunked_payload_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& stream_variant_ref,
                                                                                                            std::vector<uint8_t> payload) {  // payload 传值拷贝

            if (is_defunct()) {
                if (logger_) logger_->warn("[ConnAsyncChunking {}] Async send chunked payload on defunct connection.", get_id_for_logging());
                co_return last_error_code_;
            }

            const uint8_t* data_ptr = payload.data();
            size_t remaining_size = payload.size();
            boltprotocol::BoltError err = boltprotocol::BoltError::SUCCESS;

            std::vector<uint8_t> chunk_header_bytes(boltprotocol::CHUNK_HEADER_SIZE);

            while (remaining_size > 0) {
                uint16_t chunk_size = static_cast<uint16_t>(std::min(remaining_size, static_cast<size_t>(boltprotocol::MAX_CHUNK_PAYLOAD_SIZE)));
                uint16_t chunk_size_be = boltprotocol::detail::host_to_be(chunk_size);
                std::memcpy(chunk_header_bytes.data(), &chunk_size_be, boltprotocol::CHUNK_HEADER_SIZE);

                err = co_await _write_to_active_async_stream(stream_variant_ref, chunk_header_bytes);
                if (err != boltprotocol::BoltError::SUCCESS) break;

                std::vector<uint8_t> current_chunk_data(data_ptr, data_ptr + chunk_size);
                err = co_await _write_to_active_async_stream(stream_variant_ref, current_chunk_data);
                if (err != boltprotocol::BoltError::SUCCESS) break;

                data_ptr += chunk_size;
                remaining_size -= chunk_size;
            }

            if (err == boltprotocol::BoltError::SUCCESS) {
                uint16_t zero_chunk_be = 0;  // Correctly initialized
                std::memcpy(chunk_header_bytes.data(), &zero_chunk_be, boltprotocol::CHUNK_HEADER_SIZE);
                err = co_await _write_to_active_async_stream(stream_variant_ref, chunk_header_bytes);
            }

            // _write_to_active_async_stream -> async_io_with_timeout -> callbacks->mark_as_defunct_from_async
            // 所以这里不需要再次调用 mark_as_defunct
            co_return err;
        }

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> BoltPhysicalConnection::_receive_chunked_payload_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& stream_variant_ref) {
            std::vector<uint8_t> out_payload_vec;
            if (is_defunct()) {
                if (logger_) logger_->warn("[ConnAsyncChunking {}] Async receive chunked payload on defunct connection.", get_id_for_logging());
                co_return std::pair<boltprotocol::BoltError, std::vector<uint8_t>>{last_error_code_, {}};
            }

            size_t total_bytes_read_for_message = 0;
            boltprotocol::BoltError err = boltprotocol::BoltError::SUCCESS;
            std::pair<boltprotocol::BoltError, std::vector<uint8_t>> read_result;

            while (true) {
                read_result = co_await _read_from_active_async_stream(stream_variant_ref, boltprotocol::CHUNK_HEADER_SIZE);
                err = read_result.first;
                if (err != boltprotocol::BoltError::SUCCESS) break;

                const auto& header_bytes = read_result.second;
                if (header_bytes.size() != boltprotocol::CHUNK_HEADER_SIZE) {
                    err = boltprotocol::BoltError::NETWORK_ERROR;
                    mark_as_defunct_from_async(err, "Async receive chunk header: incorrect size read.");  // 使用接口
                    break;
                }
                uint16_t chunk_size_be;
                std::memcpy(&chunk_size_be, header_bytes.data(), boltprotocol::CHUNK_HEADER_SIZE);
                uint16_t chunk_payload_size = boltprotocol::detail::be_to_host(chunk_size_be);

                if (chunk_payload_size == 0) {
                    break;
                }
                if (chunk_payload_size > boltprotocol::MAX_CHUNK_PAYLOAD_SIZE) {
                    err = boltprotocol::BoltError::CHUNK_TOO_LARGE;
                    std::string msg = "Async received chunk larger than max: " + std::to_string(chunk_payload_size);
                    mark_as_defunct_from_async(err, msg);  // 使用接口
                    if (logger_) logger_->error("[ConnAsyncChunking {}] {}", get_id_for_logging(), msg);
                    break;
                }

                read_result = co_await _read_from_active_async_stream(stream_variant_ref, chunk_payload_size);
                err = read_result.first;
                if (err != boltprotocol::BoltError::SUCCESS) break;

                const auto& chunk_data = read_result.second;
                if (chunk_data.size() != chunk_payload_size) {
                    err = boltprotocol::BoltError::NETWORK_ERROR;
                    mark_as_defunct_from_async(err, "Async receive chunk payload: incorrect size read.");  // 使用接口
                    break;
                }
                try {
                    out_payload_vec.insert(out_payload_vec.end(), chunk_data.begin(), chunk_data.end());
                } catch (const std::bad_alloc&) {
                    err = boltprotocol::BoltError::OUT_OF_MEMORY;
                    std::string msg = "Out of memory appending async chunk to payload buffer.";
                    mark_as_defunct_from_async(err, msg);  // 使用接口
                    if (logger_) logger_->critical("[ConnAsyncChunking {}] {}", get_id_for_logging(), msg);
                    break;
                }
                total_bytes_read_for_message += chunk_payload_size;
            }

            if (err != boltprotocol::BoltError::SUCCESS) {
                co_return std::pair<boltprotocol::BoltError, std::vector<uint8_t>>{err, {}};
            } else if (total_bytes_read_for_message == 0 && out_payload_vec.empty()) {
                if (logger_) logger_->trace("[ConnAsyncChunking {}] Async received NOOP message.", get_id_for_logging());
            }
            co_return std::pair<boltprotocol::BoltError, std::vector<uint8_t>>{boltprotocol::BoltError::SUCCESS, std::move(out_payload_vec)};
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport