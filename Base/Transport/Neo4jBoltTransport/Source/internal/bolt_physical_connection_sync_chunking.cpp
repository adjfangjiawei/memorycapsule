#include <algorithm>  // For std::min
#include <cstring>    // For std::memcpy
#include <vector>

#include "boltprotocol/detail/byte_order_utils.h"
#include "boltprotocol/message_defs.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::_send_chunked_payload_sync(const std::vector<uint8_t>& payload) {
            if (is_defunct()) return last_error_code_;

            const uint8_t* data_ptr = payload.data();
            size_t remaining_size = payload.size();
            boltprotocol::BoltError err = boltprotocol::BoltError::SUCCESS;

            while (remaining_size > 0) {
                uint16_t chunk_size = static_cast<uint16_t>(std::min(remaining_size, static_cast<size_t>(boltprotocol::MAX_CHUNK_PAYLOAD_SIZE)));
                uint16_t chunk_size_be = boltprotocol::detail::host_to_be(chunk_size);

                err = _write_to_active_sync_stream(reinterpret_cast<const uint8_t*>(&chunk_size_be), boltprotocol::CHUNK_HEADER_SIZE);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    return err;
                }

                err = _write_to_active_sync_stream(data_ptr, chunk_size);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    return err;
                }

                data_ptr += chunk_size;
                remaining_size -= chunk_size;
            }

            if (err == boltprotocol::BoltError::SUCCESS) {
                uint16_t zero_chunk_be = 0;
                err = _write_to_active_sync_stream(reinterpret_cast<const uint8_t*>(&zero_chunk_be), boltprotocol::CHUNK_HEADER_SIZE);
            }
            // _write_to_active_sync_stream calls _mark_as_defunct_internal if needed
            return err;
        }

        boltprotocol::BoltError BoltPhysicalConnection::_receive_chunked_payload_sync(std::vector<uint8_t>& out_payload) {
            out_payload.clear();
            if (is_defunct()) return last_error_code_;

            size_t total_bytes_read_for_message = 0;
            boltprotocol::BoltError err = boltprotocol::BoltError::SUCCESS;

            while (true) {
                uint16_t chunk_size_be = 0;
                size_t bytes_read_header = 0;
                err = _read_from_active_sync_stream(reinterpret_cast<uint8_t*>(&chunk_size_be), boltprotocol::CHUNK_HEADER_SIZE, bytes_read_header);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    out_payload.clear();
                    return err;
                }

                uint16_t chunk_payload_size = boltprotocol::detail::be_to_host(chunk_size_be);

                if (chunk_payload_size == 0) {
                    break;
                }
                if (chunk_payload_size > boltprotocol::MAX_CHUNK_PAYLOAD_SIZE) {
                    err = boltprotocol::BoltError::CHUNK_TOO_LARGE;
                    std::string msg = "Received chunk larger than max allowed size: " + std::to_string(chunk_payload_size);
                    _mark_as_defunct_internal(err, msg);  // 使用 internal 版本
                    if (logger_) logger_->error("[ConnSyncChunking {}] {}", id_, msg);
                    out_payload.clear();
                    return err;
                }

                size_t current_payload_offset = out_payload.size();
                try {
                    out_payload.resize(current_payload_offset + chunk_payload_size);
                } catch (const std::bad_alloc&) {
                    err = boltprotocol::BoltError::OUT_OF_MEMORY;
                    std::string msg = "Out of memory resizing payload buffer for chunk.";
                    _mark_as_defunct_internal(err, msg);  // 使用 internal 版本
                    if (logger_) logger_->critical("[ConnSyncChunking {}] {}", id_, msg);
                    out_payload.clear();
                    return err;
                }

                size_t bytes_read_payload = 0;
                err = _read_from_active_sync_stream(out_payload.data() + current_payload_offset, chunk_payload_size, bytes_read_payload);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    out_payload.clear();
                    return err;
                }
                total_bytes_read_for_message += chunk_payload_size;
            }

            if (total_bytes_read_for_message == 0 && out_payload.empty()) {
                if (logger_) logger_->trace("[ConnSyncChunking {}] Received NOOP message (empty payload from chunks).", id_);
            }
            return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport