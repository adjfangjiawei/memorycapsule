#include <algorithm>             // For std::min
#include <boost/asio/read.hpp>   // For boost::asio::read with completion condition
#include <boost/asio/write.hpp>  // For boost::asio::write
#include <cstring>               // For std::memcpy
#include <iostream>              // 调试用
#include <vector>

#include "boltprotocol/detail/byte_order_utils.h"  // For host_to_be, be_to_host
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // --- Low-level I/O using active stream (plain or SSL) ---
        boltprotocol::BoltError BoltPhysicalConnection::_write_to_active_stream(const uint8_t* data, size_t size) {
            if (is_defunct()) {
                if (logger_) logger_->error("[ConnIO {}] Write attempt on defunct connection. LastError: {} ({})", id_, static_cast<int>(last_error_code_), last_error_message_);
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            boost::system::error_code ec;
            size_t bytes_written = 0;

            try {
                if (conn_config_.encryption_enabled) {  // SSL 连接
                    if (!ssl_stream_sync_ || !ssl_stream_sync_->lowest_layer().is_open()) {
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "SSL stream not open or null for write.");
                        if (logger_) logger_->error("[ConnIO {}] SSL stream not open or null for write.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnIO {}] SSL Write {} bytes", id_, size);
                    bytes_written = boost::asio::write(*ssl_stream_sync_, boost::asio::buffer(data, size), ec);
                } else {  // 明文连接
                    if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "Plain iostream wrapper not good or null for write.");
                        if (logger_) logger_->error("[ConnIO {}] Plain iostream wrapper not good or null for write.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnIO {}] Plain Write {} bytes via iostream", id_, size);
                    plain_iostream_wrapper_->write(reinterpret_cast<const char*>(data), size);
                    if (plain_iostream_wrapper_->fail()) {
                        int stream_errno = errno;
                        ec = boost::system::error_code(stream_errno != 0 ? stream_errno : EPIPE, boost::system::system_category());
                    } else {
                        bytes_written = size;
                        plain_iostream_wrapper_->flush();
                        if (plain_iostream_wrapper_->fail()) {
                            int stream_errno = errno;
                            ec = boost::system::error_code(stream_errno != 0 ? stream_errno : EPIPE, boost::system::system_category());
                        }
                    }
                }
            } catch (const boost::system::system_error& e) {
                ec = e.code();
                std::string msg = "ASIO system error during write: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::ios_base::failure& e) {
                int stream_errno = errno;
                ec = boost::system::error_code(stream_errno != 0 ? stream_errno : EPIPE, boost::system::system_category());
                std::string msg = "iostream failure during write: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            }

            if (ec) {
                std::string msg = "Write to stream failed: " + ec.message();
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            }
            if (bytes_written != size) {
                std::string msg = "Partial write to stream. Expected " + std::to_string(size) + ", wrote " + std::to_string(bytes_written);
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            }
            return boltprotocol::BoltError::SUCCESS;
        }

        boltprotocol::BoltError BoltPhysicalConnection::_read_from_active_stream(uint8_t* buffer, size_t size_to_read, size_t& bytes_read) {
            bytes_read = 0;
            if (is_defunct()) {
                if (logger_) logger_->error("[ConnIO {}] Read attempt on defunct connection. LastError: {} ({})", id_, static_cast<int>(last_error_code_), last_error_message_);
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            boost::system::error_code ec;

            try {
                if (conn_config_.encryption_enabled) {
                    if (!ssl_stream_sync_ || !ssl_stream_sync_->lowest_layer().is_open()) {
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "SSL stream not open or null for read.");
                        if (logger_) logger_->error("[ConnIO {}] SSL stream not open or null for read.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnIO {}] SSL Read {} bytes", id_, size_to_read);
                    bytes_read = boost::asio::read(*ssl_stream_sync_, boost::asio::buffer(buffer, size_to_read), ec);
                } else {
                    if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "Plain iostream wrapper not good or null for read.");
                        if (logger_) logger_->error("[ConnIO {}] Plain iostream wrapper not good or null for read.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnIO {}] Plain Read {} bytes via iostream", id_, size_to_read);
                    plain_iostream_wrapper_->read(reinterpret_cast<char*>(buffer), size_to_read);
                    bytes_read = static_cast<size_t>(plain_iostream_wrapper_->gcount());

                    if (plain_iostream_wrapper_->eof() && bytes_read < size_to_read) {
                        ec = boost::asio::error::eof;
                    } else if (plain_iostream_wrapper_->fail() && !plain_iostream_wrapper_->eof()) {
                        int stream_errno = errno;
                        ec = boost::system::error_code(stream_errno != 0 ? stream_errno : EPIPE, boost::system::system_category());
                    }
                    if (!ec && bytes_read < size_to_read) {
                        ec = boost::asio::error::eof;
                    }
                }
            } catch (const boost::system::system_error& e) {
                ec = e.code();
                std::string msg = "ASIO system error during read: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::ios_base::failure& e) {
                int stream_errno = errno;
                ec = boost::system::error_code(stream_errno != 0 ? stream_errno : EPIPE, boost::system::system_category());
                std::string msg = "iostream failure during read: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            }

            if (ec) {
                std::string msg;
                if (ec == boost::asio::error::eof) {
                    msg = "Read from stream failed: EOF reached. Read " + std::to_string(bytes_read) + "/" + std::to_string(size_to_read);
                } else {
                    msg = "Read from stream failed: " + ec.message();
                }
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            }
            if (bytes_read < size_to_read) {
                std::string msg = "Incomplete read from stream. Expected " + std::to_string(size_to_read) + ", got " + std::to_string(bytes_read);
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            }
            return boltprotocol::BoltError::SUCCESS;
        }

        boltprotocol::BoltError BoltPhysicalConnection::_send_chunked_payload(const std::vector<uint8_t>& payload) {
            if (is_defunct()) return last_error_code_;

            const uint8_t* data_ptr = payload.data();
            size_t remaining_size = payload.size();
            boltprotocol::BoltError err = boltprotocol::BoltError::SUCCESS;

            while (remaining_size > 0) {
                uint16_t chunk_size = static_cast<uint16_t>(std::min(remaining_size, static_cast<size_t>(boltprotocol::MAX_CHUNK_PAYLOAD_SIZE)));
                uint16_t chunk_size_be = boltprotocol::detail::host_to_be(chunk_size);

                err = _write_to_active_stream(reinterpret_cast<const uint8_t*>(&chunk_size_be), boltprotocol::CHUNK_HEADER_SIZE);
                if (err != boltprotocol::BoltError::SUCCESS) break;

                err = _write_to_active_stream(data_ptr, chunk_size);
                if (err != boltprotocol::BoltError::SUCCESS) break;

                data_ptr += chunk_size;
                remaining_size -= chunk_size;
            }

            if (err == boltprotocol::BoltError::SUCCESS) {
                uint16_t zero_chunk_be = 0;
                err = _write_to_active_stream(reinterpret_cast<const uint8_t*>(&zero_chunk_be), boltprotocol::CHUNK_HEADER_SIZE);
            }

            if (err != boltprotocol::BoltError::SUCCESS) {
                if (logger_ && last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                    logger_->error("[ConnIO {}] Error sending chunked payload: {}, but connection not marked defunct by IO.", id_, static_cast<int>(err));
                }
            }
            return err;
        }

        boltprotocol::BoltError BoltPhysicalConnection::_receive_chunked_payload(std::vector<uint8_t>& out_payload) {
            out_payload.clear();
            if (is_defunct()) return last_error_code_;

            size_t total_bytes_read_for_message = 0;
            boltprotocol::BoltError err = boltprotocol::BoltError::SUCCESS;

            while (true) {
                uint16_t chunk_size_be = 0;
                size_t bytes_read_header = 0;
                err = _read_from_active_stream(reinterpret_cast<uint8_t*>(&chunk_size_be), boltprotocol::CHUNK_HEADER_SIZE, bytes_read_header);
                if (err != boltprotocol::BoltError::SUCCESS) break;

                uint16_t chunk_payload_size = boltprotocol::detail::be_to_host(chunk_size_be);

                if (chunk_payload_size == 0) {
                    break;
                }
                if (chunk_payload_size > boltprotocol::MAX_CHUNK_PAYLOAD_SIZE) {
                    err = boltprotocol::BoltError::CHUNK_TOO_LARGE;
                    std::string msg = "Received chunk larger than max allowed size: " + std::to_string(chunk_payload_size);
                    _mark_as_defunct(err, msg);
                    if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                    break;
                }

                size_t current_payload_size = out_payload.size();
                try {
                    out_payload.resize(current_payload_size + chunk_payload_size);
                } catch (const std::bad_alloc&) {
                    err = boltprotocol::BoltError::OUT_OF_MEMORY;
                    std::string msg = "Out of memory resizing payload buffer for chunk.";
                    _mark_as_defunct(err, msg);
                    if (logger_) logger_->critical("[ConnIO {}] {}", id_, msg);
                    break;
                }

                size_t bytes_read_payload = 0;
                err = _read_from_active_stream(out_payload.data() + current_payload_size, chunk_payload_size, bytes_read_payload);
                if (err != boltprotocol::BoltError::SUCCESS) break;

                total_bytes_read_for_message += chunk_payload_size;
            }

            if (err != boltprotocol::BoltError::SUCCESS) {
                out_payload.clear();
            } else if (total_bytes_read_for_message == 0 && out_payload.empty()) {
                if (logger_) logger_->trace("[ConnIO {}] Received NOOP message (empty payload).", id_);
            }
            return err;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport