#include <vector>

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::establish() {
            InternalState expected_fresh = InternalState::FRESH;
            if (!current_state_.compare_exchange_strong(expected_fresh, InternalState::TCP_CONNECTING, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                InternalState current_s = current_state_.load(std::memory_order_relaxed);
                if (current_s == InternalState::READY) {
                    if (logger_) logger_->debug("[ConnLCSync {}] Establish called but connection is already READY.", id_);
                    return boltprotocol::BoltError::SUCCESS;
                }
                std::string msg = "Establish (sync) called in invalid state: " + _get_current_state_as_string() + ". Expected FRESH.";
                if (logger_) logger_->warn("[ConnLCSync {}] {}", id_, msg);
                return (current_s == InternalState::DEFUNCT) ? last_error_code_ : boltprotocol::BoltError::UNKNOWN_ERROR;
            }

            if (logger_) logger_->info("[ConnLCSync {}] Establishing (sync) connection to {}:{}", id_, conn_config_.target_host, conn_config_.target_port);
            _reset_resources_and_state(false);
            current_state_.store(InternalState::TCP_CONNECTING, std::memory_order_relaxed);

            boltprotocol::BoltError err = _stage_tcp_connect();
            if (err != boltprotocol::BoltError::SUCCESS) {
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
                if (current_state_.load(std::memory_order_relaxed) != InternalState::READY) {  // Ensure it's FRESH if not READY
                    _reset_resources_and_state(false);
                    current_state_.store(InternalState::FRESH);
                }
                return last_error_code_;
            }

            if (current_state_.load(std::memory_order_relaxed) != InternalState::READY) {
                std::string msg = "Sync connection did not reach READY state after successful establish sequence. Final state: " + _get_current_state_as_string();
                if (logger_) logger_->error("[ConnLCSync {}] {}", id_, msg);
                _mark_as_defunct_internal(boltprotocol::BoltError::UNKNOWN_ERROR, msg);  // 使用 internal
                _reset_resources_and_state(false);
                current_state_.store(InternalState::FRESH);
                return last_error_code_;
            }

            mark_as_used();
            if (last_error_code_ != boltprotocol::BoltError::SUCCESS && logger_) {
                logger_->warn("[ConnLCSync {}] Established but last_error_code_ is {}. Overriding to SUCCESS as state is READY.", id_, static_cast<int>(last_error_code_));
            }
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->info("[ConnLCSync {}] Sync Connection established and ready. Bolt version: {}.{}. Server: {}", id_, (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor, server_agent_string_);
            return boltprotocol::BoltError::SUCCESS;
        }

        boltprotocol::BoltError BoltPhysicalConnection::terminate(bool send_goodbye) {
            InternalState previous_state = current_state_.exchange(InternalState::DEFUNCT, std::memory_order_acq_rel);
            if (logger_) logger_->debug("[ConnLCSync {}] Terminating (sync). Previous state was {}. Send goodbye: {}", id_, (previous_state == InternalState::DEFUNCT ? "already DEFUNCT" : _get_current_state_as_string()), send_goodbye);

            if (previous_state == InternalState::DEFUNCT) {
                if (logger_) logger_->trace("[ConnLCSync {}] Already defunct, ensuring resources are clean.", id_);
                _reset_resources_and_state(false);
                return boltprotocol::BoltError::SUCCESS;
            }

            if (send_goodbye && previous_state >= InternalState::BOLT_HANDSHAKEN && previous_state < InternalState::DEFUNCT) {
                if (!(negotiated_bolt_version_ < boltprotocol::versions::Version(3, 0))) {
                    bool can_send = false;
                    if (conn_config_.encryption_enabled) {
                        can_send = ssl_stream_sync_ && ssl_stream_sync_->lowest_layer().is_open();
                    } else {
                        can_send = plain_iostream_wrapper_ && plain_iostream_wrapper_->good();
                    }
                    if (can_send) {
                        if (logger_) logger_->trace("[ConnLCSync {}] Attempting to send GOODBYE.", id_);
                        std::vector<uint8_t> goodbye_payload;
                        boltprotocol::PackStreamWriter ps_writer(goodbye_payload);
                        if (boltprotocol::serialize_goodbye_message(ps_writer) == boltprotocol::BoltError::SUCCESS) {
                            boltprotocol::BoltError goodbye_err = _send_chunked_payload_sync(goodbye_payload);
                            if (goodbye_err != boltprotocol::BoltError::SUCCESS && logger_) {
                                logger_->warn("[ConnLCSync {}] Sending GOODBYE failed: {}", id_, error::bolt_error_to_string(goodbye_err));
                            } else if (logger_ && goodbye_err == boltprotocol::BoltError::SUCCESS) {
                                logger_->trace("[ConnLCSync {}] GOODBYE message sent.", id_);
                            }
                        }
                    } else {
                        if (logger_) logger_->trace("[ConnLCSync {}] Cannot send GOODBYE (stream not ready or Bolt version too low). Previous state was {}.", id_, (int)previous_state);
                    }
                } else {
                    if (logger_) logger_->trace("[ConnLCSync {}] GOODBYE not applicable for Bolt version {}.{}", id_, (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor);
                }
            }
            _reset_resources_and_state(false);
            return boltprotocol::BoltError::SUCCESS;
        }

        boltprotocol::BoltError BoltPhysicalConnection::ping(std::chrono::milliseconds timeout) {
            if (logger_) logger_->debug("[ConnLCSync {}] Pinging (sync) connection (via RESET). Timeout hint: {}ms", id_, timeout.count());
            return perform_reset();
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport