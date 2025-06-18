#include <vector>

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::send_request_receive_summary(const std::vector<uint8_t>& request_payload, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure) {
            out_summary.metadata.clear();
            out_failure.metadata.clear();

            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s != InternalState::READY && current_s != InternalState::HELLO_AUTH_SENT && current_s != InternalState::BOLT_HANDSHAKEN) {
                if (logger_)
                    logger_->warn(
                        "[ConnMsgSyncSummary {}] send_request_receive_summary called in "
                        "invalid state: {}",
                        id_,
                        _get_current_state_as_string());
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            mark_as_used();

            boltprotocol::BoltError err = _send_chunked_payload_sync(request_payload);
            if (err != boltprotocol::BoltError::SUCCESS) {
                // _send_chunked_payload_sync calls _mark_as_defunct_internal
                return last_error_code_;
            }

            current_state_.store(InternalState::AWAITING_SUMMARY, std::memory_order_relaxed);

            std::vector<uint8_t> response_payload;
            while (true) {  // Loop to skip NOOPs
                err = _receive_chunked_payload_sync(response_payload);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    // _receive_chunked_payload_sync calls _mark_as_defunct_internal
                    return last_error_code_;
                }
                if (!response_payload.empty()) break;  // Got a real message
                if (logger_) logger_->trace("[ConnMsgSyncSummary {}] Received NOOP while awaiting summary.", id_);
            }

            boltprotocol::MessageTag tag;
            err = _peek_message_tag(response_payload, tag);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct_internal(err, "Failed to peek tag for summary response.");
                return last_error_code_;
            }

            boltprotocol::PackStreamReader reader(response_payload);
            if (tag == boltprotocol::MessageTag::SUCCESS) {
                err = boltprotocol::deserialize_success_message(reader, out_summary);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _mark_as_defunct_internal(err, "Failed to deserialize SUCCESS summary.");
                    return last_error_code_;
                }
                // Transition to READY only if still in AWAITING_SUMMARY (i.e., not
                // defunct by another thread/reason)
                InternalState expected_awaiting = InternalState::AWAITING_SUMMARY;
                current_state_.compare_exchange_strong(expected_awaiting, InternalState::READY, std::memory_order_acq_rel, std::memory_order_relaxed);
                last_error_code_ = boltprotocol::BoltError::SUCCESS;
                last_error_message_.clear();
                return boltprotocol::BoltError::SUCCESS;

            } else if (tag == boltprotocol::MessageTag::FAILURE) {
                err = boltprotocol::deserialize_failure_message(reader, out_failure);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _mark_as_defunct_internal(err, "Failed to deserialize FAILURE summary.");
                    return last_error_code_;
                }
                // _classify_and_set_server_failure handles state transition
                // (FAILED_SERVER_REPORTED or DEFUNCT)
                return _classify_and_set_server_failure(out_failure);

            } else if (tag == boltprotocol::MessageTag::IGNORED) {
                err = boltprotocol::deserialize_ignored_message(reader);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _mark_as_defunct_internal(err, "Failed to deserialize IGNORED summary.");
                    return last_error_code_;
                }
                // Populate a synthetic failure for the caller
                out_failure.metadata.clear();                                                           // Ensure it's empty
                out_failure.metadata["code"] = boltprotocol::Value("Neo.ClientError.Request.Ignored");  // Example
                out_failure.metadata["message"] = boltprotocol::Value("Request was ignored by the server.");
                current_state_.store(InternalState::FAILED_SERVER_REPORTED,
                                     std::memory_order_relaxed);            // Server is not defunct
                                                                            // but request failed
                last_error_code_ = boltprotocol::BoltError::UNKNOWN_ERROR;  // Or a specific IGNORED
                                                                            // code if added
                last_error_message_ = "Operation ignored by server.";
                return boltprotocol::BoltError::UNKNOWN_ERROR;  // Treat IGNORED as an operational error for summary
            } else {
                _mark_as_defunct_internal(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Unexpected message tag for summary: " + std::to_string(static_cast<int>(tag)));
                return last_error_code_;
            }
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport