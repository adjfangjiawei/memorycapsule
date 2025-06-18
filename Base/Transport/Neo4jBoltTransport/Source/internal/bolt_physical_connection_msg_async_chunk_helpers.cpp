#include <algorithm>
#include <cstring>
#include <variant>
#include <vector>

#include "boltprotocol/detail/byte_order_utils.h"
#include "neo4j_bolt_transport/internal/async_utils_decl.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/internal/i_async_context_callbacks.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // Define the static helper methods as members of BoltPhysicalConnection
        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_send_chunked_payload_async_static_helper(  // Add BoltPhysicalConnection::
            internal::ActiveAsyncStreamContext& stream_ctx,
            std::vector<uint8_t> payload,
            const BoltConnectionConfig& conn_config_ref,
            std::shared_ptr<spdlog::logger> logger_ref,
            std::function<void(boltprotocol::BoltError, const std::string&)> error_handler) {
            const uint8_t* data_ptr = payload.data();
            size_t remaining_size = payload.size();
            boltprotocol::BoltError err = boltprotocol::BoltError::SUCCESS;
            std::vector<uint8_t> chunk_header_bytes(boltprotocol::CHUNK_HEADER_SIZE);
            std::string op_name_prefix = "AsyncStaticChunkSend ";

            auto write_to_stream_lambda = [&](const std::vector<uint8_t>& data_to_write, const std::string& op_detail) -> boost::asio::awaitable<boltprotocol::BoltError> {
                std::pair<boltprotocol::BoltError, std::size_t> write_result;

                struct DummyCallbacksForWrite : public IAsyncContextCallbacks {
                    std::shared_ptr<spdlog::logger> lg;
                    uint64_t id_log = 0;
                    boltprotocol::BoltError last_err_cb = boltprotocol::BoltError::SUCCESS;
                    std::function<void(boltprotocol::BoltError, const std::string&)> main_error_handler_cb;

                    DummyCallbacksForWrite(std::shared_ptr<spdlog::logger> l, std::function<void(boltprotocol::BoltError, const std::string&)> eh) : lg(std::move(l)), main_error_handler_cb(std::move(eh)) {
                    }
                    std::shared_ptr<spdlog::logger> get_logger() const override {
                        return lg;
                    }
                    uint64_t get_id_for_logging() const override {
                        return id_log;
                    }
                    void mark_as_defunct_from_async(boltprotocol::BoltError reason, const std::string& message) override {
                        last_err_cb = reason;
                        if (main_error_handler_cb) main_error_handler_cb(reason, message);
                    }
                    boltprotocol::BoltError get_last_error_code_from_async() const override {
                        return last_err_cb;
                    }
                };
                DummyCallbacksForWrite cb_write(logger_ref, error_handler);

                write_result = co_await std::visit(
                    [&](auto& s_variant_member) -> boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::size_t>> {
                        co_return co_await async_utils::async_write_with_timeout(&cb_write, s_variant_member, boost::asio::buffer(data_to_write), std::chrono::milliseconds(conn_config_ref.socket_write_timeout_ms), op_name_prefix + op_detail);
                    },
                    stream_ctx.stream);

                if (write_result.first != boltprotocol::BoltError::SUCCESS) co_return cb_write.get_last_error_code_from_async();
                if (write_result.second != data_to_write.size()) {
                    if (error_handler) error_handler(boltprotocol::BoltError::NETWORK_ERROR, op_name_prefix + op_detail + ": Partial write.");
                    co_return boltprotocol::BoltError::NETWORK_ERROR;
                }
                co_return boltprotocol::BoltError::SUCCESS;
            };

            while (remaining_size > 0) {
                uint16_t chunk_size = static_cast<uint16_t>(std::min(remaining_size, static_cast<size_t>(boltprotocol::MAX_CHUNK_PAYLOAD_SIZE)));
                uint16_t chunk_size_be = boltprotocol::detail::host_to_be(chunk_size);
                std::memcpy(chunk_header_bytes.data(), &chunk_size_be, boltprotocol::CHUNK_HEADER_SIZE);

                err = co_await write_to_stream_lambda(chunk_header_bytes, "header");
                if (err != boltprotocol::BoltError::SUCCESS) co_return err;

                std::vector<uint8_t> current_chunk_data_vec(data_ptr, data_ptr + chunk_size);
                err = co_await write_to_stream_lambda(current_chunk_data_vec, "payload");
                if (err != boltprotocol::BoltError::SUCCESS) co_return err;

                data_ptr += chunk_size;
                remaining_size -= chunk_size;
            }

            if (err == boltprotocol::BoltError::SUCCESS) {
                uint16_t zero_chunk_be = 0;
                std::memcpy(chunk_header_bytes.data(), &zero_chunk_be, boltprotocol::CHUNK_HEADER_SIZE);
                err = co_await write_to_stream_lambda(chunk_header_bytes, "eom");
            }
            co_return err;
        }

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> BoltPhysicalConnection::_receive_chunked_payload_async_static_helper(  // Add BoltPhysicalConnection::
            internal::ActiveAsyncStreamContext& stream_ctx,
            const BoltConnectionConfig& conn_config_ref,
            std::shared_ptr<spdlog::logger> logger_ref,
            std::function<void(boltprotocol::BoltError, const std::string&)> error_handler) {
            std::vector<uint8_t> out_payload_vec;
            boltprotocol::BoltError err = boltprotocol::BoltError::SUCCESS;
            std::string op_name_prefix = "AsyncStaticChunkRecv ";

            auto read_from_stream_lambda = [&](size_t size_to_read, const std::string& op_detail) -> boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> {
                std::vector<uint8_t> temp_buffer(size_to_read);
                std::pair<boltprotocol::BoltError, std::size_t> read_result;

                struct DummyCallbacksForRead : public IAsyncContextCallbacks {
                    std::shared_ptr<spdlog::logger> lg;
                    uint64_t id_log = 0;
                    boltprotocol::BoltError last_err_cb = boltprotocol::BoltError::SUCCESS;
                    std::function<void(boltprotocol::BoltError, const std::string&)> main_error_handler_cb;
                    DummyCallbacksForRead(std::shared_ptr<spdlog::logger> l, std::function<void(boltprotocol::BoltError, const std::string&)> eh) : lg(std::move(l)), main_error_handler_cb(std::move(eh)) {
                    }
                    std::shared_ptr<spdlog::logger> get_logger() const override {
                        return lg;
                    }
                    uint64_t get_id_for_logging() const override {
                        return id_log;
                    }
                    void mark_as_defunct_from_async(boltprotocol::BoltError reason, const std::string& message) override {
                        last_err_cb = reason;
                        if (main_error_handler_cb) main_error_handler_cb(reason, message);
                    }
                    boltprotocol::BoltError get_last_error_code_from_async() const override {
                        return last_err_cb;
                    }
                };
                DummyCallbacksForRead cb_read(logger_ref, error_handler);

                read_result = co_await std::visit(
                    [&](auto& s_variant_member) -> boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::size_t>> {
                        co_return co_await async_utils::async_read_with_timeout(&cb_read, s_variant_member, boost::asio::buffer(temp_buffer), std::chrono::milliseconds(conn_config_ref.socket_read_timeout_ms), op_name_prefix + op_detail);
                    },
                    stream_ctx.stream);

                if (read_result.first != boltprotocol::BoltError::SUCCESS) {
                    co_return std::make_pair(cb_read.get_last_error_code_from_async(), std::vector<uint8_t>{});
                }
                if (read_result.second != size_to_read) {
                    if (error_handler) error_handler(boltprotocol::BoltError::NETWORK_ERROR, op_name_prefix + op_detail + ": Partial read.");
                    co_return std::make_pair(boltprotocol::BoltError::NETWORK_ERROR, std::vector<uint8_t>{});
                }
                co_return std::make_pair(boltprotocol::BoltError::SUCCESS, temp_buffer);
            };

            while (true) {
                auto [header_err, header_bytes] = co_await read_from_stream_lambda(boltprotocol::CHUNK_HEADER_SIZE, "header");
                if (header_err != boltprotocol::BoltError::SUCCESS) {
                    err = header_err;
                    break;
                }

                uint16_t chunk_size_be;
                std::memcpy(&chunk_size_be, header_bytes.data(), boltprotocol::CHUNK_HEADER_SIZE);
                uint16_t chunk_payload_size = boltprotocol::detail::be_to_host(chunk_size_be);

                if (chunk_payload_size == 0) break;
                if (chunk_payload_size > boltprotocol::MAX_CHUNK_PAYLOAD_SIZE) {
                    err = boltprotocol::BoltError::CHUNK_TOO_LARGE;
                    if (error_handler) error_handler(err, op_name_prefix + "Chunk too large.");
                    break;
                }

                auto [payload_err, chunk_data] = co_await read_from_stream_lambda(chunk_payload_size, "payload");
                if (payload_err != boltprotocol::BoltError::SUCCESS) {
                    err = payload_err;
                    break;
                }
                try {
                    out_payload_vec.insert(out_payload_vec.end(), chunk_data.begin(), chunk_data.end());
                } catch (const std::bad_alloc&) {
                    err = boltprotocol::BoltError::OUT_OF_MEMORY;
                    if (error_handler) error_handler(err, op_name_prefix + "Out of memory appending chunk.");
                    break;
                }
            }

            if (err != boltprotocol::BoltError::SUCCESS) {
                co_return std::make_pair(err, std::vector<uint8_t>{});
            }
            co_return std::make_pair(boltprotocol::BoltError::SUCCESS, std::move(out_payload_vec));
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport