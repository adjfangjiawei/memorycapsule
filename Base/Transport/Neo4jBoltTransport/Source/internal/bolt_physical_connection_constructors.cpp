#include <iostream>  // For initial logger checks
#include <utility>   // For std::move

#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        std::atomic<uint64_t> BoltPhysicalConnection::next_connection_id_counter_(0);

        BoltPhysicalConnection::BoltPhysicalConnection(BoltConnectionConfig config, boost::asio::io_context& io_ctx, std::shared_ptr<spdlog::logger> logger_ptr)
            : id_(next_connection_id_counter_++),
              conn_config_(std::move(config)),
              io_context_ref_(io_ctx),
              logger_(logger_ptr),  // Directly use passed logger
              current_state_(InternalState::FRESH),
              negotiated_bolt_version_(0, 0),
              creation_timestamp_(std::chrono::steady_clock::now()) {
            if (!logger_) {  // Fallback if a null logger was somehow passed
                std::cerr << "Warning: BoltPhysicalConnection " << id_ << " created with a null logger." << std::endl;
                // Optionally create a default emergency logger here if critical
            }

            last_used_timestamp_.store(creation_timestamp_, std::memory_order_relaxed);
            if (logger_) {
                logger_->debug("[ConnConstruct {}] Constructed. Target: {}:{}", id_, conn_config_.target_host, conn_config_.target_port);
            }
        }

        BoltPhysicalConnection::~BoltPhysicalConnection() {
            if (logger_) {
                logger_->debug("[ConnDestruct {}] Destructing. Current state: {}", id_, _get_current_state_as_string());
            }
            // Ensure resources are cleaned up. terminate(false) handles most of it.
            // _reset_resources_and_state(true) is a fallback if terminate wasn't called or state is already DEFUNCT/FRESH.
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s != InternalState::DEFUNCT && current_s != InternalState::FRESH) {
                terminate(false);  // Don't send GOODBYE from destructor
            } else {
                // If already defunct or fresh, still ensure resources are reset, especially if construction failed partway.
                _reset_resources_and_state(true);  // called_from_destructor = true
            }
            if (logger_) {
                logger_->debug("[ConnDestruct {}] Destruction complete.", id_);
            }
        }

        BoltPhysicalConnection::BoltPhysicalConnection(BoltPhysicalConnection&& other) noexcept
            : id_(other.id_),
              conn_config_(std::move(other.conn_config_)),
              io_context_ref_(other.io_context_ref_),
              logger_(std::move(other.logger_)),  // Move the logger
              owned_socket_for_sync_plain_(std::move(other.owned_socket_for_sync_plain_)),
              plain_iostream_wrapper_(std::move(other.plain_iostream_wrapper_)),
              ssl_context_sync_(std::move(other.ssl_context_sync_)),
              ssl_stream_sync_(std::move(other.ssl_stream_sync_)),
              // current_state_ is atomic, needs load/store
              negotiated_bolt_version_(other.negotiated_bolt_version_),
              server_agent_string_(std::move(other.server_agent_string_)),
              server_assigned_conn_id_(std::move(other.server_assigned_conn_id_)),
              utc_patch_active_(other.utc_patch_active_),
              creation_timestamp_(other.creation_timestamp_),
              // last_used_timestamp_ is atomic
              last_error_code_(other.last_error_code_),
              last_error_message_(std::move(other.last_error_message_)) {
            current_state_.store(other.current_state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            last_used_timestamp_.store(other.last_used_timestamp_.load(std::memory_order_relaxed), std::memory_order_relaxed);

            // Invalidate 'other' to prevent double resource management
            other.id_ = static_cast<uint64_t>(-1);  // Mark as moved-from
            other.current_state_.store(InternalState::DEFUNCT, std::memory_order_relaxed);
            // Logger in 'other' is now nullptr due to move.
            // other's unique_ptrs are now nullptr due to move.

            if (logger_) {
                logger_->trace("[ConnMoveConstruct {}] Move constructed from (now defunct) old connection {}.", id_, other.id_);
            }
        }

        BoltPhysicalConnection& BoltPhysicalConnection::operator=(BoltPhysicalConnection&& other) noexcept {
            if (this != &other) {
                if (logger_) {
                    logger_->trace("[ConnMoveAssign {}] Move assigning from old ID {}. Current state before: {}", id_, other.id_, _get_current_state_as_string());
                }
                // Properly terminate current connection's resources before overwriting
                InternalState current_s_before_assign = current_state_.load(std::memory_order_relaxed);
                if (current_s_before_assign != InternalState::DEFUNCT && current_s_before_assign != InternalState::FRESH) {
                    terminate(false);  // Gracefully terminate self if active
                } else {
                    _reset_resources_and_state(false);  // Clean up if FRESH but resources might exist
                }

                id_ = other.id_;
                conn_config_ = std::move(other.conn_config_);
                // io_context_ref_ is a reference, assumed to be compatible and not changed.
                logger_ = std::move(other.logger_);

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
                // Logger in 'other' is now nullptr.
                // other's unique_ptrs are now nullptr.

                if (logger_) {
                    logger_->trace("[ConnMoveAssign {}] Move assignment complete.", id_);
                }
            }
            return *this;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport