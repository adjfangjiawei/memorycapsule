#include <utility>  // For std::move

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/async_result_stream.h"
#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // --- AsyncResultStream Private Helper: send_discard_async ---
    boost::asio::awaitable<boltprotocol::BoltError> AsyncResultStream::send_discard_async(int64_t n, boltprotocol::SuccessMessageParams& out_discard_summary_raw) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
            logger = owner_session_->transport_manager_->get_config().logger;
        }

        if (!is_open()) {
            if (logger) logger->warn("[AsyncResultStream {}] send_discard_async on non-open stream. Failed: {}, Consumed: {}", (void*)this, stream_failed_.load(std::memory_order_acquire), stream_fully_consumed_or_discarded_.load(std::memory_order_acquire));
            co_return failure_reason_.load(std::memory_order_acquire);
        }

        boltprotocol::DiscardMessageParams discard_p;
        discard_p.n = n;
        // Assuming user has corrected this to use appropriate version check
        if (query_id_.has_value() && (stream_context_->negotiated_bolt_version.major >= 4)) {
            discard_p.qid = query_id_;
        }

        std::vector<uint8_t> discard_payload_bytes;
        boltprotocol::PackStreamWriter writer(discard_payload_bytes);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_discard_message(discard_p, writer);
        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            set_failure_state(serialize_err, "Failed to serialize DISCARD message: " + error::bolt_error_to_string(serialize_err));
            co_return failure_reason_.load(std::memory_order_acquire);
        }

        if (logger) logger->trace("[AsyncResultStream {}] Sending DISCARD (n={}, qid={})", (void*)this, n, query_id_ ? std::to_string(*query_id_) : "auto");

        auto static_op_error_handler_for_discard = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->set_failure_state(reason, "Async DISCARD operation error: " + message);
        };

        boltprotocol::BoltError send_err = co_await internal::BoltPhysicalConnection::_send_chunked_payload_async_static_helper(*stream_context_, std::move(discard_payload_bytes), stream_context_->original_config, logger, static_op_error_handler_for_discard);

        if (send_err != boltprotocol::BoltError::SUCCESS) {
            co_return failure_reason_.load(std::memory_order_acquire);
        }

        bool summary_received_for_discard = false;
        boltprotocol::BoltError final_discard_status = boltprotocol::BoltError::UNKNOWN_ERROR;

        while (!summary_received_for_discard) {
            auto [recv_err, response_payload] = co_await internal::BoltPhysicalConnection::_receive_chunked_payload_async_static_helper(*stream_context_, stream_context_->original_config, logger, static_op_error_handler_for_discard);

            if (recv_err != boltprotocol::BoltError::SUCCESS) {
                final_discard_status = failure_reason_.load(std::memory_order_acquire);
                break;
            }
            if (response_payload.empty()) {
                if (logger) logger->trace("[AsyncResultStream {}] DISCARD received NOOP.", (void*)this);
                continue;
            }

            boltprotocol::MessageTag tag;
            boltprotocol::PackStreamReader peek_reader_temp(response_payload);
            uint8_t raw_tag_byte_peek = 0;
            uint32_t num_fields_peek = 0;
            boltprotocol::BoltError peek_err = boltprotocol::peek_message_structure_header(peek_reader_temp, raw_tag_byte_peek, num_fields_peek);

            if (peek_err != boltprotocol::BoltError::SUCCESS) {
                set_failure_state(peek_err, "Failed to peek tag in DISCARD response");
                final_discard_status = failure_reason_.load(std::memory_order_acquire);
                break;
            }
            tag = static_cast<boltprotocol::MessageTag>(raw_tag_byte_peek);

            boltprotocol::PackStreamReader full_reader(response_payload);
            if (tag == boltprotocol::MessageTag::SUCCESS) {
                boltprotocol::BoltError deser_err = boltprotocol::deserialize_success_message(full_reader, out_discard_summary_raw);
                if (deser_err != boltprotocol::BoltError::SUCCESS) {
                    set_failure_state(deser_err, "Failed to deserialize SUCCESS from DISCARD");
                    final_discard_status = failure_reason_.load(std::memory_order_acquire);
                } else {
                    final_discard_status = boltprotocol::BoltError::SUCCESS;
                }
                summary_received_for_discard = true;
            } else if (tag == boltprotocol::MessageTag::FAILURE) {
                boltprotocol::FailureMessageParams discard_failure_meta;
                boltprotocol::BoltError deser_err = boltprotocol::deserialize_failure_message(full_reader, discard_failure_meta);
                if (deser_err != boltprotocol::BoltError::SUCCESS) {
                    set_failure_state(deser_err, "Failed to deserialize FAILURE from DISCARD");
                } else {
                    std::string server_fail_detail = error::format_server_failure(discard_failure_meta);
                    set_failure_state(boltprotocol::BoltError::UNKNOWN_ERROR, "Server FAILURE during DISCARD: " + server_fail_detail, discard_failure_meta);
                }
                final_discard_status = failure_reason_.load(std::memory_order_acquire);
                summary_received_for_discard = true;
            } else if (tag == boltprotocol::MessageTag::RECORD) {
                if (logger) logger->warn("[AsyncResultStream {}] Received unexpected RECORD after DISCARD. Ignoring.", (void*)this);
            } else {
                set_failure_state(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Unexpected tag " + std::to_string(static_cast<int>(tag)) + " after DISCARD");
                final_discard_status = failure_reason_.load(std::memory_order_acquire);
                summary_received_for_discard = true;
            }
        }
        co_return final_discard_status;
    }

    // --- AsyncResultStream Public Method: consume_async ---
    boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> AsyncResultStream::consume_async() {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
            logger = owner_session_->transport_manager_->get_config().logger;
        }
        if (logger) logger->trace("[AsyncResultStream {}] consume_async called.", (void*)this);

        if (stream_failed_.load(std::memory_order_acquire)) {
            co_return std::make_pair(failure_reason_.load(std::memory_order_acquire), final_summary_typed_);
        }
        if (stream_fully_consumed_or_discarded_.load(std::memory_order_acquire)) {
            co_return std::make_pair(boltprotocol::BoltError::SUCCESS, final_summary_typed_);
        }

        raw_record_buffer_.clear();

        bool needs_server_discard_op = is_first_fetch_attempt_ ? initial_server_has_more_after_run_ : server_has_more_records_after_last_pull_.load(std::memory_order_acquire);

        if (!needs_server_discard_op) {
            stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);
            if (logger) logger->trace("[AsyncResultStream {}] consume_async: No records on server to discard. Stream considered consumed.", (void*)this);
            co_return std::make_pair(boltprotocol::BoltError::SUCCESS, final_summary_typed_);
        }

        boltprotocol::SuccessMessageParams discard_summary_raw;
        boltprotocol::BoltError discard_op_err = co_await send_discard_async(-1, discard_summary_raw);

        is_first_fetch_attempt_ = false;
        stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);

        if (discard_op_err != boltprotocol::BoltError::SUCCESS) {
            co_return std::make_pair(failure_reason_.load(std::memory_order_acquire), final_summary_typed_);
        }

        update_final_summary(std::move(discard_summary_raw));
        server_has_more_records_after_last_pull_.store(false, std::memory_order_release);

        if (logger) logger->trace("[AsyncResultStream {}] consume_async successful.", (void*)this);
        co_return std::make_pair(boltprotocol::BoltError::SUCCESS, final_summary_typed_);
    }

}  // namespace neo4j_bolt_transport