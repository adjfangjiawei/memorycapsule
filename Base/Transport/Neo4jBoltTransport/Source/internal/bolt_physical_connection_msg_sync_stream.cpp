#include <functional>  // For MessageHandler
#include <vector>

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::send_request_receive_stream(const std::vector<uint8_t>& request_payload, MessageHandler record_handler, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure) {
            out_summary.metadata.clear();
            out_failure.metadata.clear();

            if (!is_ready_for_queries()) {
                if (logger_)
                    logger_->warn(
                        "[ConnMsgSyncStream {}] send_request_receive_stream called when "
                        "not ready. State: {}",
                        id_,
                        _get_current_state_as_string());
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            mark_as_used();

            boltprotocol::BoltError err = _send_chunked_payload_sync(request_payload);
            if (err != boltprotocol::BoltError::SUCCESS) {
                return last_error_code_;  // Defunct already marked
            }

            current_state_.store(InternalState::STREAMING, std::memory_order_relaxed);

            while (true) {
                std::vector<uint8_t> response_payload;
                err = _receive_chunked_payload_sync(response_payload);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    return last_error_code_;  // Defunct already marked
                }

                if (response_payload.empty()) {  // NOOP
                    if (logger_) logger_->trace("[ConnMsgSyncStream {}] Received NOOP during stream.", id_);
                    continue;
                }

                boltprotocol::MessageTag tag;
                err = _peek_message_tag(response_payload, tag);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _mark_as_defunct_internal(err, "Failed to peek tag during streaming.");
                    return last_error_code_;
                }

                if (tag == boltprotocol::MessageTag::RECORD) {
                    if (record_handler) {
                        err = record_handler(tag, response_payload,
                                             *this);  // Pass *this for BoltPhysicalConnection&
                        if (err != boltprotocol::BoltError::SUCCESS) {
                            std::string msg = "Record handler returned error: " + error::bolt_error_to_string(err);
                            _mark_as_defunct_internal(err, msg);
                            return last_error_code_;
                        }
                    } else {
                        _mark_as_defunct_internal(boltprotocol::BoltError::INVALID_ARGUMENT, "Received RECORD but no handler provided for stream.");
                        return last_error_code_;
                    }
                } else if (tag == boltprotocol::MessageTag::SUCCESS) {
                    current_state_.store(InternalState::AWAITING_SUMMARY,
                                         std::memory_order_relaxed);  // Intermediate
                    boltprotocol::PackStreamReader reader(response_payload);
                    err = boltprotocol::deserialize_success_message(reader, out_summary);
                    if (err != boltprotocol::BoltError::SUCCESS) {
                        _mark_as_defunct_internal(err, "Failed to deserialize SUCCESS summary in stream.");
                        return last_error_code_;
                    }
                    InternalState expected_state_before_ready = InternalState::AWAITING_SUMMARY;
                    current_state_.compare_exchange_strong(expected_state_before_ready, InternalState::READY, std::memory_order_acq_rel, std::memory_order_relaxed);
                    last_error_code_ = boltprotocol::BoltError::SUCCESS;
                    last_error_message_.clear();
                    return boltprotocol::BoltError::SUCCESS;

                } else if (tag == boltprotocol::MessageTag::FAILURE) {
                    current_state_.store(InternalState::AWAITING_SUMMARY, std::memory_order_relaxed);
                    boltprotocol::PackStreamReader reader(response_payload);
                    err = boltprotocol::deserialize_failure_message(reader, out_failure);
                    if (err != boltprotocol::BoltError::SUCCESS) {
                        _mark_as_defunct_internal(err, "Failed to deserialize FAILURE summary in stream.");
                        return last_error_code_;
                    }
                    return _classify_and_set_server_failure(out_failure);

                } else if (tag == boltprotocol::MessageTag::IGNORED) {
                    current_state_.store(InternalState::AWAITING_SUMMARY, std::memory_order_relaxed);
                    boltprotocol::PackStreamReader reader(response_payload);
                    err = boltprotocol::deserialize_ignored_message(reader);
                    if (err != boltprotocol::BoltError::SUCCESS) {
                        _mark_as_defunct_internal(err, "Failed to deserialize IGNORED summary in stream.");
                        return last_error_code_;
                    }
                    out_failure.metadata.clear();
                    out_failure.metadata["code"] = boltprotocol::Value("Neo.ClientError.Request.Ignored");
                    out_failure.metadata["message"] = boltprotocol::Value("Request was ignored by the server during stream.");
                    current_state_.store(InternalState::FAILED_SERVER_REPORTED, std::memory_order_relaxed);
                    last_error_code_ = boltprotocol::BoltError::UNKNOWN_ERROR;
                    last_error_message_ = "Operation ignored by server during stream.";
                    return boltprotocol::BoltError::UNKNOWN_ERROR;

                } else {
                    _mark_as_defunct_internal(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Unexpected message tag in stream: " + std::to_string(static_cast<int>(tag)));
                    return last_error_code_;
                }
            }
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport