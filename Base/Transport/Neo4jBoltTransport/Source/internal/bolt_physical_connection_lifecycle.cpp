#include <boost/asio.hpp>  // For co_spawn, detached, use_awaitable etc.
#include <iostream>
#include <utility>  // For std::move

#include "boltprotocol/message_serialization.h"  // For serialize_goodbye_message
#include "boltprotocol/packstream_writer.h"      // For PackStreamWriter
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        std::atomic<uint64_t> BoltPhysicalConnection::next_connection_id_counter_(0);

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
            // terminate will call _reset_resources_and_state
            if (current_state_.load(std::memory_order_relaxed) != InternalState::DEFUNCT && current_state_.load(std::memory_order_relaxed) != InternalState::FRESH) {  // FRESH with no resources is fine
                terminate(false);                                                                                                                                      // Don't send GOODBYE from destructor usually
            } else {
                _reset_resources_and_state(true);  // Ensure cleanup even if FRESH but resources were partially allocated
            }
        }

        BoltPhysicalConnection::BoltPhysicalConnection(BoltPhysicalConnection&& other) noexcept
            : id_(other.id_),
              conn_config_(std::move(other.conn_config_)),
              io_context_ref_(other.io_context_ref_),  // io_context_ref_ is a reference
              logger_(std::move(other.logger_)),
              owned_socket_for_sync_plain_(std::move(other.owned_socket_for_sync_plain_)),
              plain_iostream_wrapper_(std::move(other.plain_iostream_wrapper_)),
              ssl_context_sync_(std::move(other.ssl_context_sync_)),
              ssl_stream_sync_(std::move(other.ssl_stream_sync_)),
              negotiated_bolt_version_(other.negotiated_bolt_version_),
              server_agent_string_(std::move(other.server_agent_string_)),
              server_assigned_conn_id_(std::move(other.server_assigned_conn_id_)),
              utc_patch_active_(other.utc_patch_active_),
              creation_timestamp_(other.creation_timestamp_),
              last_error_code_(other.last_error_code_),
              last_error_message_(std::move(other.last_error_message_)) {
            current_state_.store(other.current_state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            last_used_timestamp_.store(other.last_used_timestamp_.load(std::memory_order_relaxed), std::memory_order_relaxed);

            // Invalidate the 'other' object to prevent double frees or incorrect state
            other.id_ = static_cast<uint64_t>(-1);                                          // Or some other invalid marker
            other.current_state_.store(InternalState::DEFUNCT, std::memory_order_relaxed);  // Mark other as moved-from and defunct
            // Do not null out other.logger_ as it might be shared and still in use by the moved-to object.
            // Null out unique_ptrs in 'other' as they have been moved.
            other.owned_socket_for_sync_plain_ = nullptr;
            other.plain_iostream_wrapper_ = nullptr;
            other.ssl_context_sync_ = nullptr;
            other.ssl_stream_sync_ = nullptr;

            if (logger_) {  // Use the logger_ of the *newly constructed* object
                logger_->trace("[ConnLC {}] Move constructed from (now defunct) old connection.", id_);
            }
        }

        BoltPhysicalConnection& BoltPhysicalConnection::operator=(BoltPhysicalConnection&& other) noexcept {
            if (this != &other) {
                if (logger_) {  // logger_ of 'this' object before assignment
                    logger_->trace("[ConnLC {}] Move assigning from old ID {}. Current state before: {}", id_, other.id_, _get_current_state_as_string());
                }
                // Properly terminate current connection before overwriting
                if (current_state_.load(std::memory_order_relaxed) != InternalState::DEFUNCT && current_state_.load(std::memory_order_relaxed) != InternalState::FRESH) {
                    terminate(false);
                } else {
                    _reset_resources_and_state(false);  // Clean up if FRESH but resources might exist
                }

                id_ = other.id_;
                conn_config_ = std::move(other.conn_config_);
                // io_context_ref_ is a reference, cannot be re-assigned. Assume it's compatible.
                logger_ = std::move(other.logger_);  // Logger is moved

                owned_socket_for_sync_plain_ = std::move(other.owned_socket_for_sync_plain_);
                plain_iostream_wrapper_ = std::move(other.plain_iostream_wrapper_);
                ssl_context_sync_ = std::move(other.ssl_context_sync_);
                ssl_stream_sync_ = std::move(other.ssl_stream_sync_);

                current_state_.store(other.current_state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                negotiated_bolt_version_ = other.negotiated_bolt_version_;
                server_agent_string_ = std::move(other.server_agent_string_);
                server_assigned_conn_id_ = std::move(other.server_assigned_conn_id_);
                utc_patch_active_ = other.utc_patch_active_;
                creation_timestamp_ = other.creation_timestamp_;
                last_used_timestamp_.store(other.last_used_timestamp_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                last_error_code_ = other.last_error_code_;
                last_error_message_ = std::move(other.last_error_message_);

                // Invalidate 'other'
                other.id_ = static_cast<uint64_t>(-1);
                other.current_state_.store(InternalState::DEFUNCT, std::memory_order_relaxed);
                other.owned_socket_for_sync_plain_ = nullptr;
                other.plain_iostream_wrapper_ = nullptr;
                other.ssl_context_sync_ = nullptr;
                other.ssl_stream_sync_ = nullptr;

                if (logger_) {  // Use the logger_ of 'this' (newly assigned) object
                    logger_->trace("[ConnLC {}] Move assignment complete.", id_);
                }
            }
            return *this;
        }

        void BoltPhysicalConnection::_reset_resources_and_state(bool /*called_from_destructor*/) {
            // This function is called from establish() before connecting, terminate(), destructor, and move assignment.
            // It should bring the connection to a clean, Fresh-like state regarding resources.

            // Close and release SSL stream first, then plain iostream, then the underlying socket.
            if (ssl_stream_sync_) {
                boost::system::error_code ec_ssl_shutdown, ec_tcp_close;
                if (ssl_stream_sync_->lowest_layer().is_open()) {
                    // Only attempt SSL shutdown if not already implicitly shutdown by peer or error
                    if (!(SSL_get_shutdown(ssl_stream_sync_->native_handle()) & (SSL_RECEIVED_SHUTDOWN | SSL_SENT_SHUTDOWN))) {
                        try {
                            ssl_stream_sync_->shutdown(ec_ssl_shutdown);
                        } catch (const boost::system::system_error& e) {
                            if (logger_) logger_->trace("[ConnLC {}] Exception during SSL shutdown: {}", id_, e.what());
                        }
                    }
                    // lowest_layer().close() is handled by plain_iostream_wrapper_ or owned_socket if SSL stream was wrapping it.
                    // Here, ssl_stream_sync_ *owns* the socket if it was created via std::move from owned_socket_for_sync_plain_.
                    // So, closing the lowest layer of ssl_stream_sync_ is appropriate here if it's open.
                    try {
                        ssl_stream_sync_->lowest_layer().close(ec_tcp_close);
                    } catch (const boost::system::system_error& e) {
                        if (logger_) logger_->trace("[ConnLC {}] Exception during SSL lowest_layer close: {}", id_, e.what());
                    }
                }
                ssl_stream_sync_.reset();
            }
            if (ssl_context_sync_) {
                ssl_context_sync_.reset();
            }

            if (plain_iostream_wrapper_) {
                // iostream owns its socket, destroying it will close the socket.
                plain_iostream_wrapper_.reset();
            }

            // If owned_socket_for_sync_plain_ still exists, it means it wasn't moved to iostream or ssl_stream.
            if (owned_socket_for_sync_plain_) {
                if (owned_socket_for_sync_plain_->is_open()) {
                    boost::system::error_code ec;
                    try {
                        owned_socket_for_sync_plain_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                    } catch (...) { /* ignore */
                    }
                    try {
                        owned_socket_for_sync_plain_->close(ec);
                    } catch (...) { /* ignore */
                    }
                }
                owned_socket_for_sync_plain_.reset();
            }

            negotiated_bolt_version_ = boltprotocol::versions::Version(0, 0);
            server_agent_string_.clear();
            server_assigned_conn_id_.clear();
            utc_patch_active_ = false;

            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();

            // Only set to FRESH if not already DEFUNCT (e.g. from destructor or explicit terminate)
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s != InternalState::DEFUNCT) {
                current_state_.store(InternalState::FRESH, std::memory_order_relaxed);
            }
            // last_used_timestamp_ is updated when used or by constructor. No need to reset here.
            if (logger_) logger_->trace("[ConnLC {}] Resources reset. State is now: {}", id_, _get_current_state_as_string());
        }

        boltprotocol::BoltError BoltPhysicalConnection::establish() {
            InternalState expected_fresh = InternalState::FRESH;
            // Attempt to transition from FRESH to TCP_CONNECTING
            if (!current_state_.compare_exchange_strong(expected_fresh, InternalState::TCP_CONNECTING, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                InternalState current_s = current_state_.load(std::memory_order_relaxed);  // Read the actual current state
                if (current_s == InternalState::READY) {
                    if (logger_) logger_->debug("[ConnLC {}] Establish called but connection is already READY.", id_);
                    return boltprotocol::BoltError::SUCCESS;
                }
                // If not FRESH and not READY, it's an invalid state to call establish()
                std::string msg = "Establish called in invalid state: " + _get_current_state_as_string() + ". Expected FRESH.";
                if (logger_) logger_->warn("[ConnLC {}] {}", id_, msg);
                // Do not _mark_as_defunct here if it was, for example, already connecting by another thread.
                // This needs careful thought on concurrent establish calls. For now, assume single-threaded establish attempt per object.
                return (current_s == InternalState::DEFUNCT) ? last_error_code_ : boltprotocol::BoltError::UNKNOWN_ERROR;
            }

            if (logger_) logger_->info("[ConnLC {}] Establishing connection to {}:{}", id_, conn_config_.target_host, conn_config_.target_port);

            // _reset_resources_and_state(false); // Should be done before CAS or if CAS fails and retrying.
            // Since we successfully CASed from FRESH, assume resources are already reset or this is the first attempt.
            // If establish can be re-called on a non-FRESH (but not DEFUNCT) object, _reset_resources_and_state is critical.
            // Given current logic, if not FRESH, it errors out. So, this is for the FRESH path.

            boltprotocol::BoltError err = _stage_tcp_connect();
            if (err != boltprotocol::BoltError::SUCCESS) {
                // _stage_tcp_connect already calls _mark_as_defunct on failure.
                // _reset_resources_and_state will be called by terminate() or destructor eventually.
                // Or if we want to allow re-calling establish on a failed-to-connect object, reset here.
                _reset_resources_and_state(false);           // Ensure clean state for potential retry by caller
                current_state_.store(InternalState::FRESH);  // Reset to FRESH for a new establish attempt
                return last_error_code_;
            }

            if (conn_config_.encryption_enabled) {
                err = _stage_ssl_context_setup();
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _reset_resources_and_state(false);
                    current_state_.store(InternalState::FRESH);
                    return last_error_code_;
                }
                err = _stage_ssl_handshake();
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _reset_resources_and_state(false);
                    current_state_.store(InternalState::FRESH);
                    return last_error_code_;
                }
            }

            err = _stage_bolt_handshake();
            if (err != boltprotocol::BoltError::SUCCESS) {
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                return last_error_code_;
            }

            err = _stage_send_hello_and_initial_auth();
            if (err != boltprotocol::BoltError::SUCCESS) {
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                return last_error_code_;
            }

            // If all stages passed, current_state_ should be READY (set by _stage_send_hello_and_initial_auth on success)
            if (current_state_.load(std::memory_order_relaxed) != InternalState::READY) {
                std::string msg = "Connection did not reach READY state after successful establish sequence. Final state: " + _get_current_state_as_string();
                if (logger_) logger_->error("[ConnLC {}] {}", id_, msg);
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);  // This sets state to DEFUNCT
                _reset_resources_and_state(false);                              // Clean up
                current_state_.store(InternalState::FRESH);                     // Allow re-try
                return last_error_code_;
            }

            mark_as_used();                                       // Successfully established and ready
            last_error_code_ = boltprotocol::BoltError::SUCCESS;  // Ensure this is set
            last_error_message_.clear();
            if (logger_) logger_->info("[ConnLC {}] Connection established and ready. Bolt version: {}.{}. Server: {}", id_, (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor, server_agent_string_);
            return boltprotocol::BoltError::SUCCESS;
        }

        boltprotocol::BoltError BoltPhysicalConnection::terminate(bool send_goodbye) {
            InternalState previous_state = current_state_.exchange(InternalState::DEFUNCT, std::memory_order_acq_rel);
            if (logger_) logger_->debug("[ConnLC {}] Terminating. Previous state: {}. Send goodbye: {}", id_, _get_current_state_as_string(), send_goodbye);

            if (previous_state == InternalState::DEFUNCT) {
                if (logger_) logger_->trace("[ConnLC {}] Already defunct, ensuring resources are clean.", id_);
                _reset_resources_and_state(false);  // Ensure cleanup
                return boltprotocol::BoltError::SUCCESS;
            }

            // Attempt to send GOODBYE only if connection was in a state where it's meaningful
            if (send_goodbye && previous_state >= InternalState::BOLT_HANDSHAKEN && previous_state < InternalState::DEFUNCT) {
                if (!(negotiated_bolt_version_ < boltprotocol::versions::Version(3, 0))) {  // GOODBYE for Bolt 3.0+
                    bool can_send = false;
                    if (conn_config_.encryption_enabled) {
                        can_send = ssl_stream_sync_ && ssl_stream_sync_->lowest_layer().is_open();
                    } else {
                        can_send = plain_iostream_wrapper_ && plain_iostream_wrapper_->good();
                    }

                    if (can_send) {
                        if (logger_) logger_->trace("[ConnLC {}] Attempting to send GOODBYE.", id_);
                        std::vector<uint8_t> goodbye_payload;
                        boltprotocol::PackStreamWriter ps_writer(goodbye_payload);
                        if (boltprotocol::serialize_goodbye_message(ps_writer) == boltprotocol::BoltError::SUCCESS) {
                            // _send_chunked_payload is synchronous. Errors are handled internally.
                            // We are already DEFUNCT, so _mark_as_defunct inside _send_chunked_payload won't re-trigger.
                            boltprotocol::BoltError goodbye_err = _send_chunked_payload(goodbye_payload);
                            if (goodbye_err != boltprotocol::BoltError::SUCCESS && logger_) {
                                logger_->warn("[ConnLC {}] Sending GOODBYE failed: {}", id_, error::bolt_error_to_string(goodbye_err));
                            } else if (logger_ && goodbye_err == boltprotocol::BoltError::SUCCESS) {
                                logger_->trace("[ConnLC {}] GOODBYE message sent.", id_);
                            }
                        }
                    } else {
                        if (logger_) logger_->trace("[ConnLC {}] Cannot send GOODBYE (stream not ready or Bolt version too low). Previous state: {}", id_, (int)previous_state);
                    }
                } else {
                    if (logger_) logger_->trace("[ConnLC {}] GOODBYE not applicable for Bolt version {}.{}", id_, (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor);
                }
            }

            _reset_resources_and_state(false);  // Clean up all resources
            return boltprotocol::BoltError::SUCCESS;
        }

        void BoltPhysicalConnection::mark_as_used() {
            last_used_timestamp_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
        }

        bool BoltPhysicalConnection::is_encrypted() const {
            // Considered encrypted if config expects it, SSL stream exists, and handshake was done.
            return conn_config_.encryption_enabled && (ssl_stream_sync_ != nullptr) && (current_state_.load(std::memory_order_relaxed) >= InternalState::SSL_HANDSHAKEN);
        }

        // --- Asynchronous Lifecycle Methods ---
        // Placeholder implementation for establish_async
        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::establish_async() {
            if (logger_) logger_->debug("[ConnLCAsync {}] establish_async called. Target: {}:{}", id_, conn_config_.target_host, conn_config_.target_port);

            // This is a very simplified placeholder. A full async establish would:
            // 1. Manage an async-specific socket/stream (e.g., a member unique_ptr for the async stream)
            // 2. Call the _async versions of each stage:
            //    _stage_tcp_connect_async(async_socket, timeout)
            //    _stage_ssl_context_setup() (still mostly sync)
            //    _stage_ssl_handshake_async(async_ssl_stream_wrapping_async_socket, timeout)
            //    _stage_bolt_handshake_async(async_stream, timeout)
            //    _stage_send_hello_and_initial_auth_async(async_stream)
            // 3. Handle timeouts and errors for each async step.

            // For now, just return an error indicating it's not fully implemented.
            // To make this compile, we need to ensure all co_await calls resolve.
            // A true async establish would likely manage its own socket instance for the duration of the coroutine.

            // Attempting a more structured placeholder:
            InternalState expected_fresh = InternalState::FRESH;
            if (!current_state_.compare_exchange_strong(expected_fresh, InternalState::TCP_CONNECTING)) {
                if (logger_) logger_->warn("[ConnLCAsync {}] establish_async called in invalid state: {}", id_, _get_current_state_as_string());
                co_return boltprotocol::BoltError::UNKNOWN_ERROR;
            }

            // 1. Create an async socket (typically managed by the coroutine or a dedicated member)
            // boost::asio::ip::tcp::socket async_socket(co_await boost::asio::this_coro::executor);
            // For this placeholder, we won't actually use it for I/O.

            // Call placeholder async stages (these would take the async_socket/stream)
            // auto tcp_err = co_await _stage_tcp_connect_async(async_socket, std::chrono::milliseconds(conn_config_.tcp_connect_timeout_ms));
            // if (tcp_err != boltprotocol::BoltError::SUCCESS) {
            //     _reset_resources_and_state(false); current_state_.store(InternalState::FRESH); co_return tcp_err;
            // }
            // if (conn_config_.encryption_enabled) {
            //     _stage_ssl_context_setup(); // Assuming sync for now
            //     // boost::asio::ssl::stream<boost::asio::ip::tcp::socket&> async_ssl_stream(async_socket, *ssl_context_sync_);
            //     // auto ssl_hs_err = co_await _stage_ssl_handshake_async(async_ssl_stream, std::chrono::milliseconds(conn_config_.bolt_handshake_timeout_ms));
            //     // if (ssl_hs_err != boltprotocol::BoltError::SUCCESS) { /* ... */ co_return ssl_hs_err; }
            // }
            // ... and so on for Bolt handshake and HELLO/auth ...

            if (logger_) logger_->error("[ConnLCAsync {}] establish_async is a non-functional placeholder.", id_);
            _reset_resources_and_state(false);
            current_state_.store(InternalState::FRESH);
            co_return boltprotocol::BoltError::UNKNOWN_ERROR;  // Placeholder
        }

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::terminate_async(bool send_goodbye) {
            InternalState previous_state = current_state_.exchange(InternalState::DEFUNCT, std::memory_order_acq_rel);
            if (logger_) logger_->debug("[ConnLCAsync {}] Terminating async. Previous state: {}. Send goodbye: {}", id_, _get_current_state_as_string(), send_goodbye);

            if (previous_state == InternalState::DEFUNCT) {
                _reset_resources_and_state(false);
                co_return boltprotocol::BoltError::SUCCESS;
            }

            if (send_goodbye && previous_state >= InternalState::BOLT_HANDSHAKEN && previous_state < InternalState::DEFUNCT && !(negotiated_bolt_version_ < boltprotocol::versions::Version(3, 0))) {
                // Asynchronous GOODBYE sending would use _send_chunked_payload_async
                std::vector<uint8_t> goodbye_payload_storage;
                boltprotocol::PackStreamWriter ps_writer(goodbye_payload_storage);
                if (boltprotocol::serialize_goodbye_message(ps_writer) == boltprotocol::BoltError::SUCCESS) {
                    // Assuming _send_chunked_payload_async is available and works on the active async stream
                    // boltprotocol::BoltError goodbye_err = co_await _send_chunked_payload_async(std::move(goodbye_payload_storage));
                    // if (goodbye_err != boltprotocol::BoltError::SUCCESS && logger_) {
                    //     logger_->warn("[ConnLCAsync {}] Sending GOODBYE async failed: {}", id_, error::bolt_error_to_string(goodbye_err));
                    // }
                    if (logger_) logger_->trace("[ConnLCAsync {}] Placeholder for async GOODBYE message.", id_);
                }
            }

            _reset_resources_and_state(false);  // Resource cleanup is synchronous
            co_return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport