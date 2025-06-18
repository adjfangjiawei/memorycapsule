#include <algorithm>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <cstring>
#include <iostream>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::_write_to_active_sync_stream(const uint8_t* data, size_t size) {
            if (is_defunct()) {
                if (logger_) logger_->error("[ConnSyncIO {}] Write attempt on defunct connection. LastError: {} ({})", id_, static_cast<int>(last_error_code_), last_error_message_);
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            boost::system::error_code ec;
            size_t bytes_written = 0;

            try {
                if (conn_config_.encryption_enabled) {
                    if (!ssl_stream_sync_ || !ssl_stream_sync_->lowest_layer().is_open()) {
                        _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, "SSL stream not open or null for write.");
                        if (logger_) logger_->error("[ConnSyncIO {}] SSL stream not open or null for write.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnSyncIO {}] SSL Write {} bytes", id_, size);
                    bytes_written = boost::asio::write(*ssl_stream_sync_, boost::asio::buffer(data, size), ec);
                } else {
                    if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                        _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, "Plain iostream wrapper not good or null for write.");
                        if (logger_) logger_->error("[ConnSyncIO {}] Plain iostream wrapper not good or null for write.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnSyncIO {}] Plain Write {} bytes via iostream", id_, size);
                    plain_iostream_wrapper_->write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
                    if (plain_iostream_wrapper_->fail()) {
                        int stream_errno = errno;
                        ec = boost::system::error_code(stream_errno != 0 ? stream_errno : EPIPE, boost::system::system_category());
                    } else {
                        bytes_written = size;
                        plain_iostream_wrapper_->flush();
                        if (plain_iostream_wrapper_->fail()) {
                            int stream_errno_flush = errno;
                            ec = boost::system::error_code(stream_errno_flush != 0 ? stream_errno_flush : EPIPE, boost::system::system_category());
                        }
                    }
                }
            } catch (const boost::system::system_error& e) {
                ec = e.code();
                std::string msg = "ASIO system error during sync write: " + std::string(e.what());
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSyncIO {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::ios_base::failure& e) {
                int stream_errno = errno;
                ec = boost::system::error_code(stream_errno != 0 ? stream_errno : EIO, boost::system::system_category());
                std::string msg = "iostream failure during sync write: " + std::string(e.what());
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSyncIO {}] {}", id_, msg);
                return last_error_code_;
            }

            if (ec) {
                std::string msg = "Sync write to stream failed: " + ec.message();
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSyncIO {}] {}", id_, msg);
                return last_error_code_;
            }
            if (bytes_written != size) {
                std::string msg = "Partial sync write to stream. Expected " + std::to_string(size) + ", wrote " + std::to_string(bytes_written);
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSyncIO {}] {}", id_, msg);
                return last_error_code_;
            }
            return boltprotocol::BoltError::SUCCESS;
        }

        boltprotocol::BoltError BoltPhysicalConnection::_read_from_active_sync_stream(uint8_t* buffer, size_t size_to_read, size_t& bytes_read) {
            bytes_read = 0;
            if (is_defunct()) {
                if (logger_) logger_->error("[ConnSyncIO {}] Read attempt on defunct connection. LastError: {} ({})", id_, static_cast<int>(last_error_code_), last_error_message_);
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            if (size_to_read == 0) return boltprotocol::BoltError::SUCCESS;

            boost::system::error_code ec;

            try {
                if (conn_config_.encryption_enabled) {
                    if (!ssl_stream_sync_ || !ssl_stream_sync_->lowest_layer().is_open()) {
                        _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, "SSL stream not open or null for read.");
                        if (logger_) logger_->error("[ConnSyncIO {}] SSL stream not open or null for read.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnSyncIO {}] SSL Read {} bytes", id_, size_to_read);
                    bytes_read = boost::asio::read(*ssl_stream_sync_, boost::asio::buffer(buffer, size_to_read), ec);
                } else {
                    if (!plain_iostream_wrapper_ || !plain_iostream_wrapper_->good()) {
                        _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, "Plain iostream wrapper not good or null for read.");
                        if (logger_) logger_->error("[ConnSyncIO {}] Plain iostream wrapper not good or null for read.", id_);
                        return last_error_code_;
                    }
                    if (logger_) logger_->trace("[ConnSyncIO {}] Plain Read {} bytes via iostream", id_, size_to_read);
                    plain_iostream_wrapper_->read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size_to_read));
                    bytes_read = static_cast<size_t>(plain_iostream_wrapper_->gcount());

                    if (bytes_read < size_to_read) {
                        if (plain_iostream_wrapper_->eof()) {
                            ec = boost::asio::error::eof;
                        } else if (plain_iostream_wrapper_->fail()) {
                            int stream_errno = errno;
                            ec = boost::system::error_code(stream_errno != 0 ? stream_errno : EIO, boost::system::system_category());
                        } else {
                            ec = boost::asio::error::fault;
                        }
                    }
                }
            } catch (const boost::system::system_error& e) {
                ec = e.code();
                std::string msg = "ASIO system error during sync read: " + std::string(e.what());
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSyncIO {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::ios_base::failure& e) {
                int stream_errno = errno;
                ec = boost::system::error_code(stream_errno != 0 ? stream_errno : EIO, boost::system::system_category());
                std::string msg = "iostream failure during sync read: " + std::string(e.what());
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSyncIO {}] {}", id_, msg);
                return last_error_code_;
            }

            if (ec) {
                std::string msg;
                if (ec == boost::asio::error::eof) {
                    msg = "Sync read from stream failed: EOF reached prematurely. Read " + std::to_string(bytes_read) + "/" + std::to_string(size_to_read);
                } else {
                    msg = "Sync read from stream failed: " + ec.message() + ". Read " + std::to_string(bytes_read) + "/" + std::to_string(size_to_read);
                }
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSyncIO {}] {}", id_, msg);
                return last_error_code_;
            }
            if (bytes_read < size_to_read) {
                std::string msg = "Incomplete sync read from stream. Expected " + std::to_string(size_to_read) + ", got " + std::to_string(bytes_read) + ". No specific stream error reported.";
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSyncIO {}] {}", id_, msg);
                return last_error_code_;
            }
            return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport