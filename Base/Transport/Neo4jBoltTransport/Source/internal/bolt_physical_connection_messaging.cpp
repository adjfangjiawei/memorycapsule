#include <iostream>
#include <vector>

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        // _peek_message_tag: 查看反序列化前响应负载中的消息标签
        boltprotocol::BoltError BoltPhysicalConnection::_peek_message_tag(const std::vector<uint8_t>& payload, boltprotocol::MessageTag& out_tag) const {
            if (payload.empty()) {
                return boltprotocol::BoltError::INVALID_MESSAGE_FORMAT;
            }
            boltprotocol::PackStreamReader temp_reader(payload);
            uint8_t raw_tag_byte = 0;
            uint32_t num_fields = 0;

            boltprotocol::BoltError peek_err = boltprotocol::peek_message_structure_header(temp_reader, raw_tag_byte, num_fields);
            if (peek_err != boltprotocol::BoltError::SUCCESS) {
                return peek_err;
            }
            out_tag = static_cast<boltprotocol::MessageTag>(raw_tag_byte);
            return boltprotocol::BoltError::SUCCESS;
        }

        // send_request_receive_summary: 发送请求并期望单个摘要响应 (SUCCESS, FAILURE, IGNORED)
        boltprotocol::BoltError BoltPhysicalConnection::send_request_receive_summary(const std::vector<uint8_t>& request_payload, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure) {
            out_summary.metadata.clear();
            out_failure.metadata.clear();

            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s != InternalState::READY && current_s != InternalState::HELLO_AUTH_SENT && current_s != InternalState::BOLT_HANDSHAKEN) {
                if (logger_) logger_->warn("[ConnMsg {}] send_request_receive_summary called in invalid state: {}", id_, _get_current_state_as_string());
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            mark_as_used();

            boltprotocol::BoltError err = _send_chunked_payload(request_payload);
            if (err != boltprotocol::BoltError::SUCCESS) {
                return last_error_code_;
            }

            current_state_.store(InternalState::AWAITING_SUMMARY, std::memory_order_relaxed);

            std::vector<uint8_t> response_payload;
            while (true) {
                err = _receive_chunked_payload(response_payload);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    return last_error_code_;
                }
                if (!response_payload.empty()) break;
                if (logger_) logger_->trace("[ConnMsg {}] Received NOOP while awaiting summary.", id_);
            }

            boltprotocol::MessageTag tag;
            err = _peek_message_tag(response_payload, tag);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct(err, "Failed to peek tag for summary response.");
                return last_error_code_;
            }

            boltprotocol::PackStreamReader reader(response_payload);
            if (tag == boltprotocol::MessageTag::SUCCESS) {
                err = boltprotocol::deserialize_success_message(reader, out_summary);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _mark_as_defunct(err, "Failed to deserialize SUCCESS summary.");
                    return last_error_code_;
                }
                if (current_state_.load(std::memory_order_relaxed) == InternalState::AWAITING_SUMMARY) {
                    current_state_.store(InternalState::READY, std::memory_order_relaxed);
                }
                last_error_code_ = boltprotocol::BoltError::SUCCESS;
                last_error_message_.clear();
                return boltprotocol::BoltError::SUCCESS;

            } else if (tag == boltprotocol::MessageTag::FAILURE) {
                err = boltprotocol::deserialize_failure_message(reader, out_failure);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _mark_as_defunct(err, "Failed to deserialize FAILURE summary.");
                    return last_error_code_;
                }
                return _classify_and_set_server_failure(out_failure);

            } else if (tag == boltprotocol::MessageTag::IGNORED) {
                err = boltprotocol::deserialize_ignored_message(reader);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _mark_as_defunct(err, "Failed to deserialize IGNORED summary.");
                    return last_error_code_;
                }
                out_failure.metadata.clear();
                out_failure.metadata["code"] = boltprotocol::Value("Neo.ClientError.Request.Ignored");
                out_failure.metadata["message"] = boltprotocol::Value("Request was ignored by the server.");
                current_state_.store(InternalState::FAILED_SERVER_REPORTED, std::memory_order_relaxed);
                last_error_code_ = boltprotocol::BoltError::SUCCESS;
                last_error_message_ = "Operation ignored by server.";
                return boltprotocol::BoltError::SUCCESS;

            } else {
                _mark_as_defunct(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Unexpected message tag for summary: " + std::to_string(static_cast<int>(tag)));
                return last_error_code_;
            }
        }

        boltprotocol::BoltError BoltPhysicalConnection::send_request_receive_stream(const std::vector<uint8_t>& request_payload, MessageHandler record_handler, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure) {
            out_summary.metadata.clear();
            out_failure.metadata.clear();

            if (!is_ready_for_queries()) {
                if (logger_) logger_->warn("[ConnMsg {}] send_request_receive_stream called when not ready. State: {}", id_, _get_current_state_as_string());
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            mark_as_used();

            boltprotocol::BoltError err = _send_chunked_payload(request_payload);
            if (err != boltprotocol::BoltError::SUCCESS) {
                return last_error_code_;
            }

            current_state_.store(InternalState::STREAMING, std::memory_order_relaxed);

            while (true) {
                std::vector<uint8_t> response_payload;
                err = _receive_chunked_payload(response_payload);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    return last_error_code_;
                }

                if (response_payload.empty()) {
                    if (logger_) logger_->trace("[ConnMsg {}] Received NOOP during stream.", id_);
                    continue;
                }

                boltprotocol::MessageTag tag;
                err = _peek_message_tag(response_payload, tag);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _mark_as_defunct(err, "Failed to peek tag during streaming.");
                    return last_error_code_;
                }

                if (tag == boltprotocol::MessageTag::RECORD) {
                    if (record_handler) {
                        err = record_handler(tag, response_payload, *this);
                        if (err != boltprotocol::BoltError::SUCCESS) {
                            std::string msg = "Record handler returned error: " + error::bolt_error_to_string(err);
                            _mark_as_defunct(err, msg);
                            return last_error_code_;
                        }
                    } else {
                        _mark_as_defunct(boltprotocol::BoltError::INVALID_ARGUMENT, "Received RECORD but no handler provided.");
                        return last_error_code_;
                    }
                } else if (tag == boltprotocol::MessageTag::SUCCESS) {
                    current_state_.store(InternalState::AWAITING_SUMMARY, std::memory_order_relaxed);
                    boltprotocol::PackStreamReader reader(response_payload);
                    err = boltprotocol::deserialize_success_message(reader, out_summary);
                    if (err != boltprotocol::BoltError::SUCCESS) {
                        _mark_as_defunct(err, "Failed to deserialize SUCCESS summary in stream.");
                        return last_error_code_;
                    }
                    current_state_.store(InternalState::READY, std::memory_order_relaxed);
                    last_error_code_ = boltprotocol::BoltError::SUCCESS;
                    last_error_message_.clear();
                    return boltprotocol::BoltError::SUCCESS;

                } else if (tag == boltprotocol::MessageTag::FAILURE) {
                    current_state_.store(InternalState::AWAITING_SUMMARY, std::memory_order_relaxed);
                    boltprotocol::PackStreamReader reader(response_payload);
                    err = boltprotocol::deserialize_failure_message(reader, out_failure);
                    if (err != boltprotocol::BoltError::SUCCESS) {
                        _mark_as_defunct(err, "Failed to deserialize FAILURE summary in stream.");
                        return last_error_code_;
                    }
                    return _classify_and_set_server_failure(out_failure);

                } else if (tag == boltprotocol::MessageTag::IGNORED) {
                    current_state_.store(InternalState::AWAITING_SUMMARY, std::memory_order_relaxed);
                    boltprotocol::PackStreamReader reader(response_payload);
                    err = boltprotocol::deserialize_ignored_message(reader);
                    if (err != boltprotocol::BoltError::SUCCESS) {
                        _mark_as_defunct(err, "Failed to deserialize IGNORED summary in stream.");
                        return last_error_code_;
                    }
                    out_failure.metadata.clear();
                    out_failure.metadata["code"] = boltprotocol::Value("Neo.ClientError.Request.Ignored");
                    out_failure.metadata["message"] = boltprotocol::Value("Request was ignored by the server.");
                    current_state_.store(InternalState::FAILED_SERVER_REPORTED, std::memory_order_relaxed);
                    last_error_code_ = boltprotocol::BoltError::SUCCESS;
                    last_error_message_ = "Operation ignored by server.";
                    return boltprotocol::BoltError::SUCCESS;

                } else {
                    _mark_as_defunct(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Unexpected message tag in stream: " + std::to_string(static_cast<int>(tag)));
                    return last_error_code_;
                }
            }
        }

        boltprotocol::BoltError BoltPhysicalConnection::perform_reset() {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (current_s == InternalState::DEFUNCT || current_s < InternalState::BOLT_HANDSHAKEN) {
                if (logger_) logger_->warn("[ConnMsg {}] perform_reset called in unsuitable state: {}", id_, _get_current_state_as_string());
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::NETWORK_ERROR;
            }
            if (logger_) logger_->debug("[ConnMsg {}] Performing RESET...", id_);
            mark_as_used();

            std::vector<uint8_t> reset_payload_bytes;
            boltprotocol::PackStreamWriter writer(reset_payload_bytes);
            boltprotocol::BoltError err = boltprotocol::serialize_reset_message(writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                _mark_as_defunct(err, "RESET serialization failed.");
                return last_error_code_;
            }

            boltprotocol::SuccessMessageParams success_meta;
            boltprotocol::FailureMessageParams failure_meta;

            err = send_request_receive_summary(reset_payload_bytes, success_meta, failure_meta);

            if (err == boltprotocol::BoltError::SUCCESS && last_error_code_ == boltprotocol::BoltError::SUCCESS) {
                if (current_state_.load(std::memory_order_relaxed) == InternalState::READY) {
                    if (logger_) logger_->info("[ConnMsg {}] RESET successful. Connection is READY.", id_);
                    return boltprotocol::BoltError::SUCCESS;
                } else {
                    std::string msg = "RESET completed exchange but connection not READY. State: " + _get_current_state_as_string();
                    if (logger_) logger_->error("[ConnMsg {}] {}", id_, msg);
                    _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                    return last_error_code_;
                }
            } else {
                if (logger_) logger_->error("[ConnMsg {}] RESET failed. Error: {}, Msg: {}", id_, static_cast<int>(last_error_code_), last_error_message_);
                return last_error_code_;
            }
        }

        boltprotocol::BoltError BoltPhysicalConnection::ping(std::chrono::milliseconds /*timeout_placeholder*/) {
            // PING 通常通过发送 RESET 并期望成功来实现
            // 超时主要应用于网络操作，这里的 RESET 调用 send_request_receive_summary，
            // 而后者内部的网络操作（_send_chunked_payload, _receive_chunked_payload）
            // 目前没有显式的超时控制（依赖于底层的TCP超时）。
            // 如果要实现严格的 PING 超时，需要在 send_request_receive_summary 中加入超时机制，
            // 或者在调用 RESET 之前/之后设置一个定时器来中断操作（这对于同步代码来说比较复杂）。
            if (logger_) logger_->debug("[ConnMsg {}] Pinging connection (via RESET)...", id_);
            return perform_reset();
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport