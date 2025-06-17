// Source/internal/bolt_physical_connection_lifecycle.cpp
#include <iostream>
#include <utility>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // For error::bolt_error_to_string
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // ... (Constructor, Destructor, Move Constructor as previously corrected) ...
        BoltPhysicalConnection::BoltPhysicalConnection(BoltConnectionConfig config, boost::asio::io_context& io_ctx, std::shared_ptr<spdlog::logger> logger_ptr)
            : id_(next_connection_id_counter_++), conn_config_(std::move(config)), io_context_ref_(io_ctx), logger_(std::move(logger_ptr)), current_state_(InternalState::FRESH), negotiated_bolt_version_(0, 0), creation_timestamp_(std::chrono::steady_clock::now()) {
            last_used_timestamp_.store(creation_timestamp_, std::memory_order_relaxed);
            if (logger_) {
                logger_->debug("[ConnLC {}] Constructed. Target: {}:{}", id_, conn_config_.target_host, conn_config_.target_port);
            }
        }

        BoltPhysicalConnection::~BoltPhysicalConnection() {
            if (logger_) {
                logger_->debug("[ConnLC {}] Destructing. Current state: {}", id_, _get_current_state_as_string());
            }
            terminate(false);
        }

        BoltPhysicalConnection::BoltPhysicalConnection(BoltPhysicalConnection&& other) noexcept
            : id_(other.id_),
              conn_config_(std::move(other.conn_config_)),
              io_context_ref_(other.io_context_ref_),  // Reference is copied (refers to the same io_context)
              logger_(std::move(other.logger_)),
              owned_socket_(std::move(other.owned_socket_)),
              plain_iostream_wrapper_(std::move(other.plain_iostream_wrapper_)),
              ssl_context_(std::move(other.ssl_context_)),
              ssl_stream_(std::move(other.ssl_stream_)),
              chunked_writer_(std::move(other.chunked_writer_)),
              chunked_reader_(std::move(other.chunked_reader_)),
              negotiated_bolt_version_(other.negotiated_bolt_version_),
              server_agent_string_(std::move(other.server_agent_string_)),
              server_assigned_conn_id_(std::move(other.server_assigned_conn_id_)),
              utc_patch_active_(other.utc_patch_active_),
              creation_timestamp_(other.creation_timestamp_),
              last_error_code_(other.last_error_code_),
              last_error_message_(std::move(other.last_error_message_)) {
            current_state_.store(other.current_state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            last_used_timestamp_.store(other.last_used_timestamp_.load(std::memory_order_relaxed), std::memory_order_relaxed);

            other.id_ = static_cast<uint64_t>(-1);
            other.current_state_.store(InternalState::DEFUNCT, std::memory_order_relaxed);
            if (logger_) {
                logger_->trace("[ConnLC {}] Move constructed from (now defunct) old connection.", id_);
            }
        }

        BoltPhysicalConnection& BoltPhysicalConnection::operator=(BoltPhysicalConnection&& other) noexcept {
            if (this != &other) {
                if (logger_) {
                    logger_->trace("[ConnLC {}] Move assigning from old ID {}. Current state before: {}", id_, other.id_, _get_current_state_as_string());
                }
                terminate(false);

                // io_context_ref_ remains bound to the io_context of *this* object's construction.
                // It's a reference and cannot be reassigned.
                // If you need to associate with other's io_context, BoltPhysicalConnection
                // would need a different design (e.g. pass io_context by shared_ptr or raw ptr and manage lifetime).
                // For typical use, a connection is bound to one io_context.

                id_ = other.id_;
                conn_config_ = std::move(other.conn_config_);
                // logger_ can be moved
                logger_ = std::move(other.logger_);
                owned_socket_ = std::move(other.owned_socket_);
                plain_iostream_wrapper_ = std::move(other.plain_iostream_wrapper_);
                ssl_context_ = std::move(other.ssl_context_);
                ssl_stream_ = std::move(other.ssl_stream_);
                chunked_writer_ = std::move(other.chunked_writer_);
                chunked_reader_ = std::move(other.chunked_reader_);
                current_state_.store(other.current_state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                negotiated_bolt_version_ = other.negotiated_bolt_version_;
                server_agent_string_ = std::move(other.server_agent_string_);
                server_assigned_conn_id_ = std::move(other.server_assigned_conn_id_);
                utc_patch_active_ = other.utc_patch_active_;
                creation_timestamp_ = other.creation_timestamp_;
                last_used_timestamp_.store(other.last_used_timestamp_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                last_error_code_ = other.last_error_code_;
                last_error_message_ = std::move(other.last_error_message_);

                other.id_ = static_cast<uint64_t>(-1);
                other.current_state_.store(InternalState::DEFUNCT, std::memory_order_relaxed);
                if (logger_) {
                    logger_->trace("[ConnLC {}] Move assignment complete.", id_);
                }
            }
            return *this;
        }

        // ... (_reset_resources_and_state, establish as previously corrected) ...
        void BoltPhysicalConnection::_reset_resources_and_state(bool called_from_destructor) {
            if (logger_) logger_->trace("[ConnLC {}] Resetting resources. From dtor: {}. Current state: {}", id_, called_from_destructor, _get_current_state_as_string());

            negotiated_bolt_version_ = boltprotocol::versions::Version(0, 0);
            server_agent_string_.clear();
            server_assigned_conn_id_.clear();
            utc_patch_active_ = false;

            chunked_reader_.reset();
            chunked_writer_.reset();

            if (ssl_stream_) {
                boost::system::error_code ec_ssl_shutdown, ec_tcp_close, ec_tcp_shutdown;
                auto& lowest_socket = ssl_stream_->lowest_layer();

                if (lowest_socket.is_open()) {
                    if (!called_from_destructor) {
                        try {
                            if (!(SSL_get_shutdown(ssl_stream_->native_handle()) & SSL_RECEIVED_SHUTDOWN)) {
                                ssl_stream_->shutdown(ec_ssl_shutdown);
                            }
                        } catch (const boost::system::system_error& e) {
                            if (logger_ && !ec_ssl_shutdown && e.code()) ec_ssl_shutdown = e.code();
                            if (logger_ && ec_ssl_shutdown) logger_->warn("[ConnLC {}] SSL shutdown error (ignored during reset): {}", id_, ec_ssl_shutdown.message());
                        }
                    }
                    lowest_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec_tcp_shutdown);
                    lowest_socket.close(ec_tcp_close);
                }
                ssl_stream_.reset();
            }
            ssl_context_.reset();

            if (plain_iostream_wrapper_) {
                if (!called_from_destructor && plain_iostream_wrapper_->good()) {
                    try {
                        plain_iostream_wrapper_->flush();
                    } catch (const std::ios_base::failure& e) {
                        if (logger_) logger_->warn("[ConnLC {}] Plain stream flush error (ignored during reset): {}", id_, e.what());
                    }
                }
                plain_iostream_wrapper_.reset();
            }

            if (owned_socket_ && owned_socket_->is_open()) {
                if (logger_) logger_->warn("[ConnLC {}] owned_socket_ was unexpectedly open during reset, closing.", id_);
                boost::system::error_code ec;
                owned_socket_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                owned_socket_->close(ec);
            }
            owned_socket_.reset();

            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            current_state_.store(InternalState::FRESH, std::memory_order_relaxed);
            last_used_timestamp_.store(creation_timestamp_, std::memory_order_relaxed);
        }

        boltprotocol::BoltError BoltPhysicalConnection::establish() {
            InternalState expected_fresh = InternalState::FRESH;
            if (!current_state_.compare_exchange_strong(expected_fresh, InternalState::TCP_CONNECTING, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                if (current_state_.load(std::memory_order_relaxed) == InternalState::READY) {
                    if (logger_) logger_->debug("[ConnLC {}] Establish called but already READY.", id_);
                    return boltprotocol::BoltError::SUCCESS;
                }
                std::string msg = "Establish called in invalid state " + _get_current_state_as_string();
                if (logger_) logger_->warn("[ConnLC {}] {}", id_, msg);
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                return last_error_code_;
            }
            if (logger_) logger_->info("[ConnLC {}] Establishing connection to {}:{}", id_, conn_config_.target_host, conn_config_.target_port);

            boltprotocol::BoltError err = _stage_tcp_connect();
            if (err != boltprotocol::BoltError::SUCCESS) {
                _reset_resources_and_state(false);
                return last_error_code_;
            }

            if (conn_config_.encryption_enabled) {
                err = _stage_ssl_context_setup();
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _reset_resources_and_state(false);
                    return last_error_code_;
                }
                err = _stage_ssl_handshake();
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _reset_resources_and_state(false);
                    return last_error_code_;
                }
            }

            err = _stage_bolt_handshake();
            if (err != boltprotocol::BoltError::SUCCESS) {
                _reset_resources_and_state(false);
                return last_error_code_;
            }

            err = _stage_send_hello_and_initial_auth();
            if (err != boltprotocol::BoltError::SUCCESS) {
                _reset_resources_and_state(false);
                return last_error_code_;
            }

            if (current_state_.load(std::memory_order_relaxed) != InternalState::READY) {
                std::string msg = "Connection did not reach READY state after establish sequence. Final state: " + _get_current_state_as_string();
                if (logger_) logger_->error("[ConnLC {}] {}", id_, msg);
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                _reset_resources_and_state(false);
                return last_error_code_;
            }

            mark_as_used();
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->info("[ConnLC {}] Connection established and ready. Bolt version: {}.{}. Server: {}", id_, (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor, server_agent_string_);
            return boltprotocol::BoltError::SUCCESS;
        }

        boltprotocol::BoltError BoltPhysicalConnection::terminate(bool send_goodbye) {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnLC {}] Terminating. Current state: {}. Send goodbye: {}", id_, _get_current_state_as_string(), send_goodbye);

            if (current_s == InternalState::FRESH && !plain_iostream_wrapper_ && !ssl_stream_ && !owned_socket_) {
                return boltprotocol::BoltError::SUCCESS;
            }

            InternalState expected_s_for_cas = current_s;
            while (expected_s_for_cas != InternalState::DEFUNCT && !current_state_.compare_exchange_weak(expected_s_for_cas, InternalState::DEFUNCT, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            }

            if (send_goodbye && current_s >= InternalState::BOLT_HANDSHAKEN && current_s != InternalState::DEFUNCT && !(negotiated_bolt_version_ < boltprotocol::versions::Version(3, 0))) {
                bool can_send_goodbye = false;
                if (conn_config_.encryption_enabled) {
                    if (ssl_stream_ && ssl_stream_->lowest_layer().is_open()) can_send_goodbye = true;
                } else {
                    if (plain_iostream_wrapper_ && plain_iostream_wrapper_->good()) can_send_goodbye = true;
                }

                if (can_send_goodbye) {
                    if (logger_) logger_->trace("[ConnLC {}] Attempting to send GOODBYE.", id_);
                    std::vector<uint8_t> goodbye_payload;
                    boltprotocol::PackStreamWriter ps_writer(goodbye_payload);
                    if (boltprotocol::serialize_goodbye_message(ps_writer) == boltprotocol::BoltError::SUCCESS) {
                        boltprotocol::BoltError goodbye_err = _send_chunked_payload(goodbye_payload);
                        if (goodbye_err != boltprotocol::BoltError::SUCCESS && logger_) {
                            // Use error::bolt_error_to_string
                            logger_->warn("[ConnLC {}] Sending GOODBYE failed: {}", id_, error::bolt_error_to_string(goodbye_err));
                        }
                    }
                } else {
                    if (logger_) logger_->trace("[ConnLC {}] Cannot send GOODBYE (stream not ready or Bolt version too low).", id_);
                }
            }
            _reset_resources_and_state(false);
            return boltprotocol::BoltError::SUCCESS;
        }

        void BoltPhysicalConnection::mark_as_used() {
            last_used_timestamp_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
        }

        bool BoltPhysicalConnection::is_encrypted() const {
            return conn_config_.encryption_enabled && (ssl_stream_ != nullptr) && (current_state_.load(std::memory_order_relaxed) >= InternalState::SSL_HANDSHAKEN);
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport