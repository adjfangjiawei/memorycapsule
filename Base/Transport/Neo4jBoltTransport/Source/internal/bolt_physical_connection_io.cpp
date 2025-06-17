#include <boost/asio/read.hpp>   // For boost::asio::read with completion condition
#include <boost/asio/write.hpp>  // For boost::asio::write
#include <iostream>              // For debug, replace with logging
#include <vector>

#include "boltprotocol/detail/byte_order_utils.h"  // For host_to_be, be_to_host
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // --- Low-level I/O using active stream (plain or SSL) ---
        boltprotocol::BoltError BoltPhysicalConnection::_write_to_active_stream(const uint8_t* data, size_t size) {
            if (is_defunct()) {  // is_defunct also checks stream validity indirectly
                if (logger_) logger_->error("[ConnIO {}] Write attempt on defunct connection. LastError: {}", id_, static_cast<int>(last_error_code_));
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            boost::system::error_code ec;
            size_t bytes_written = 0;

            try {
                if (conn_config_.encryption_enabled) {
                    if (!ssl_stream_ || !ssl_stream_->lowest_layer().is_open()) {
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "SSL stream not open or null for write.");
                        if (logger_) logger_->error("[ConnIO {}] SSL stream not open or null for write.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnIO {}] SSL Write {} bytes", id_, size);
                    bytes_written = boost::asio::write(*ssl_stream_, boost::asio::buffer(data, size), ec);
                } else {  // Plain connection
                    if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "Plain iostream wrapper not good or null for write.");
                        if (logger_) logger_->error("[ConnIO {}] Plain iostream wrapper not good or null for write.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnIO {}] Plain Write {} bytes via iostream", id_, size);
                    plain_iostream_wrapper_->write(reinterpret_cast<const char*>(data), size);
                    if (plain_iostream_wrapper_->fail()) {
                        ec = boost::system::error_code(errno ? errno : EPIPE, boost::system::system_category());  // EPIPE if fail but no errno
                    } else {
                        bytes_written = size;                   // iostream write is all-or-nothing or throws/sets fail
                        plain_iostream_wrapper_->flush();       // Crucial for iostream
                        if (plain_iostream_wrapper_->fail()) {  // Check fail after flush
                            ec = boost::system::error_code(errno ? errno : EPIPE, boost::system::system_category());
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
                ec = boost::system::error_code(errno ? errno : EPIPE, boost::system::system_category());
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
            // For SSL, boost::asio::write ensures all bytes are written or an error is set.
            // For iostream, we assume success if no failbit and bytes_written == size.
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
                if (logger_) logger_->error("[ConnIO {}] Read attempt on defunct connection. LastError: {}", id_, static_cast<int>(last_error_code_));
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            boost::system::error_code ec;

            try {
                if (conn_config_.encryption_enabled) {
                    if (!ssl_stream_ || !ssl_stream_->lowest_layer().is_open()) {
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "SSL stream not open or null for read.");
                        if (logger_) logger_->error("[ConnIO {}] SSL stream not open or null for read.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnIO {}] SSL Read {} bytes", id_, size_to_read);
                    bytes_read = boost::asio::read(*ssl_stream_, boost::asio::buffer(buffer, size_to_read), ec);
                } else {  // Plain connection
                    if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "Plain iostream wrapper not good or null for read.");
                        if (logger_) logger_->error("[ConnIO {}] Plain iostream wrapper not good or null for read.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnIO {}] Plain Read {} bytes via iostream", id_, size_to_read);
                    plain_iostream_wrapper_->read(reinterpret_cast<char*>(buffer), size_to_read);
                    bytes_read = static_cast<size_t>(plain_iostream_wrapper_->gcount());

                    if (plain_iostream_wrapper_->eof() && bytes_read < size_to_read) {
                        ec = boost::asio::error::eof;                                                 // EOF reached before all bytes read
                    } else if (plain_iostream_wrapper_->fail() && !plain_iostream_wrapper_->eof()) {  // Other stream error
                        ec = boost::system::error_code(errno ? errno : EPIPE, boost::system::system_category());
                    }
                    // If no error, bytes_read holds the count. boost::asio::read semantics (all or error) is desired.
                    // For iostream, if gcount() < size_to_read and no error bits set, it's a short read.
                    if (!ec && bytes_read < size_to_read) {  // Treat short read as an error for exact reads
                        ec = boost::asio::error::eof;        // Or a custom "short read" error
                    }
                }
            } catch (const boost::system::system_error& e) {
                ec = e.code();
                std::string msg = "ASIO system error during read: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::ios_base::failure& e) {
                ec = boost::system::error_code(errno ? errno : EPIPE, boost::system::system_category());
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
            // For exact reads, ensure all requested bytes were read by boost::asio::read
            // or by our check for iostream short reads.
            if (bytes_read < size_to_read) {  // This should ideally be caught by 'ec' setting above
                std::string msg = "Incomplete read from stream. Expected " + std::to_string(size_to_read) + ", got " + std::to_string(bytes_read);
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnIO {}] {}", id_, msg);
                return last_error_code_;
            }
            return boltprotocol::BoltError::SUCCESS;
        }

        // --- Chunking logic ---
        // (These should be largely okay now that _write_to_active_stream and _read_from_active_stream are fixed)
        // _send_chunked_payload and _receive_chunked_payload remain as previously corrected,
        // using the fixed _write_to_active_stream and _read_from_active_stream.

        boltprotocol::BoltError BoltPhysicalConnection::_send_chunked_payload(const std::vector<uint8_t>& payload) {
            if (is_defunct()) return last_error_code_;  // Redundant if called by _write_to_active_stream which checks too

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
                // _write_to_active_stream already called _mark_as_defunct and logged
                if (logger_ && last_error_code_ == boltprotocol::BoltError::SUCCESS) {  // If _write didn't mark defunct itself
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
                // _read_from_active_stream ensures bytes_read_header == CHUNK_HEADER_SIZE on success

                uint16_t chunk_payload_size = boltprotocol::detail::be_to_host(chunk_size_be);

                if (chunk_payload_size == 0) {  // End of message
                    break;
                }
                if (chunk_payload_size > boltprotocol::MAX_CHUNK_PAYLOAD_SIZE) {
                    err = boltprotocol::BoltError::CHUNK_TOO_LARGE;
                    _mark_as_defunct(err, "Received chunk larger than max allowed size: " + std::to_string(chunk_payload_size));
                    if (logger_) logger_->error("[ConnIO {}] {}", id_, get_last_error_message());
                    break;
                }

                size_t current_payload_size = out_payload.size();
                try {
                    out_payload.resize(current_payload_size + chunk_payload_size);
                } catch (const std::bad_alloc&) {
                    err = boltprotocol::BoltError::OUT_OF_MEMORY;
                    _mark_as_defunct(err, "Out of memory resizing payload buffer for chunk.");
                    if (logger_) logger_->critical("[ConnIO {}] {}", id_, get_last_error_message());
                    break;
                }

                size_t bytes_read_payload = 0;
                err = _read_from_active_stream(out_payload.data() + current_payload_size, chunk_payload_size, bytes_read_payload);
                if (err != boltprotocol::BoltError::SUCCESS) break;
                // _read_from_active_stream ensures bytes_read_payload == chunk_payload_size on success

                total_bytes_read_for_message += chunk_payload_size;
                // TODO: Add check for MAX_MESSAGE_SIZE if applicable, to prevent OOM from malicious server
            }

            if (err != boltprotocol::BoltError::SUCCESS) {
                // _read_from_active_stream or other logic already called _mark_as_defunct and logged
                out_payload.clear();  // Ensure payload is cleared on error
            } else if (total_bytes_read_for_message == 0 && out_payload.empty()) {
                // Received only a zero-size chunk (NOOP). Caller handles this.
                if (logger_) logger_->trace("[ConnIO {}] Received NOOP message (empty payload).", id_);
            }
            return err;
        }

        // --- Public Message Exchange APIs ---
        // Implementations for send_request_receive_stream, send_request_receive_summary, perform_reset
        // are in bolt_physical_connection_messaging.cpp and should now use these corrected IO methods.
        // PING is also in _messaging.cpp.

    }  // namespace internal
}  // namespace neo4j_bolt_transport