#include <utility>

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/async_result_stream.h"
#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // --- ensure_records_buffered_async (from previous batch, user has corrected version checks) ---
    boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, bool>> AsyncResultStream::ensure_records_buffered_async() {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
            logger = owner_session_->transport_manager_->get_config().logger;
        }

        if (stream_failed_.load(std::memory_order_acquire)) {
            co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
        }
        if (stream_fully_consumed_or_discarded_.load(std::memory_order_acquire)) {
            co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", false);
        }
        if (!raw_record_buffer_.empty()) {
            co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", true);
        }

        bool effectively_has_more_on_server = is_first_fetch_attempt_ ? initial_server_has_more_after_run_ : server_has_more_records_after_last_pull_.load(std::memory_order_acquire);

        if (!effectively_has_more_on_server) {
            stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);
            if (is_first_fetch_attempt_ && logger) {
                logger->trace("[AsyncResultStream {}] ensure_records_buffered: No initial records and RUN summary indicated no more.", (void*)this);
            }
            co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", false);
        }

        if (!stream_context_ || !std::visit(
                                    [](auto& s_ref) {
                                        return s_ref.lowest_layer().is_open();
                                    },
                                    stream_context_->stream)) {
            set_failure_state(boltprotocol::BoltError::NETWORK_ERROR, "Stream context invalid or closed before fetching more records.");
            co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
        }

        int64_t fetch_n = session_config_cache_.default_fetch_size > 0 ? session_config_cache_.default_fetch_size : 1000;
        if (session_config_cache_.default_fetch_size == -1) fetch_n = -1;

        if (logger) logger->trace("[AsyncResultStream {}] Buffer empty, fetching next batch (n={}). FirstFetch: {}", (void*)this, fetch_n, is_first_fetch_attempt_);

        boltprotocol::PullMessageParams pull_p;
        pull_p.n = fetch_n;
        if (query_id_.has_value() && (stream_context_->negotiated_bolt_version.major >= 4)) {
            pull_p.qid = query_id_;
        }
        std::vector<uint8_t> pull_payload_bytes;
        boltprotocol::PackStreamWriter pull_writer(pull_payload_bytes);
        boltprotocol::BoltError serialize_err = boltprotocol::serialize_pull_message(pull_p, pull_writer);
        if (serialize_err != boltprotocol::BoltError::SUCCESS) {
            set_failure_state(serialize_err, "Failed to serialize PULL for buffering: " + error::bolt_error_to_string(serialize_err));
            co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
        }

        auto static_op_error_handler_for_stream_iteration = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            this->set_failure_state(reason, "Async PULL (buffering) op error: " + message);
        };

        boltprotocol::BoltError send_err = co_await internal::BoltPhysicalConnection::_send_chunked_payload_async_static_helper(*stream_context_,
                                                                                                                                std::move(pull_payload_bytes),  // Pass by value and move
                                                                                                                                stream_context_->original_config,
                                                                                                                                logger,
                                                                                                                                static_op_error_handler_for_stream_iteration);

        if (send_err != boltprotocol::BoltError::SUCCESS) {
            co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
        }
        is_first_fetch_attempt_ = false;

        bool current_pull_summary_received = false;
        while (!current_pull_summary_received) {
            auto [recv_err, response_payload] = co_await internal::BoltPhysicalConnection::_receive_chunked_payload_async_static_helper(*stream_context_, stream_context_->original_config, logger, static_op_error_handler_for_stream_iteration);

            if (recv_err != boltprotocol::BoltError::SUCCESS) {
                co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
            }
            if (response_payload.empty()) {
                if (logger) logger->trace("[AsyncResultStream {}] ensure_records_buffered: Received NOOP.", (void*)this);
                continue;
            }

            boltprotocol::MessageTag tag;
            boltprotocol::PackStreamReader peek_reader_temp(response_payload);
            uint8_t raw_tag_byte_peek = 0;
            uint32_t num_fields_peek = 0;
            boltprotocol::BoltError peek_err = boltprotocol::peek_message_structure_header(peek_reader_temp, raw_tag_byte_peek, num_fields_peek);
            if (peek_err != boltprotocol::BoltError::SUCCESS) {
                set_failure_state(peek_err, "Failed to peek tag in PULL response (buffering)");
                co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
            }
            tag = static_cast<boltprotocol::MessageTag>(raw_tag_byte_peek);

            boltprotocol::PackStreamReader full_reader(response_payload);
            if (tag == boltprotocol::MessageTag::RECORD) {
                boltprotocol::RecordMessageParams rec_params;
                boltprotocol::BoltError deser_rec_err = boltprotocol::deserialize_record_message(full_reader, rec_params);
                if (deser_rec_err != boltprotocol::BoltError::SUCCESS) {
                    set_failure_state(deser_rec_err, "Failed to deserialize RECORD in PULL (buffering)");
                    co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
                }
                raw_record_buffer_.push_back(std::move(rec_params));
            } else if (tag == boltprotocol::MessageTag::SUCCESS) {
                boltprotocol::SuccessMessageParams pull_summary_meta;
                boltprotocol::BoltError deser_succ_err = boltprotocol::deserialize_success_message(full_reader, pull_summary_meta);
                if (deser_succ_err != boltprotocol::BoltError::SUCCESS) {
                    set_failure_state(deser_succ_err, "Failed to deserialize SUCCESS from PULL (buffering)");
                    co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
                }
                update_final_summary(std::move(pull_summary_meta));

                auto it_has_more = final_summary_typed_.raw_params().metadata.find("has_more");
                if (it_has_more != final_summary_typed_.raw_params().metadata.end() && std::holds_alternative<bool>(it_has_more->second)) {
                    server_has_more_records_after_last_pull_.store(std::get<bool>(it_has_more->second), std::memory_order_release);
                } else {
                    server_has_more_records_after_last_pull_.store(false, std::memory_order_release);
                }
                current_pull_summary_received = true;
                if (logger) logger->trace("[AsyncResultStream {}] PULL (buffering) SUCCESS received. HasMore: {}", (void*)this, server_has_more_records_after_last_pull_.load(std::memory_order_acquire));
            } else if (tag == boltprotocol::MessageTag::FAILURE) {
                boltprotocol::FailureMessageParams pull_failure_meta;
                boltprotocol::BoltError deser_fail_err = boltprotocol::deserialize_failure_message(full_reader, pull_failure_meta);
                if (deser_fail_err != boltprotocol::BoltError::SUCCESS) {
                    set_failure_state(deser_fail_err, "Failed to deserialize FAILURE from PULL (buffering)");
                } else {
                    std::string server_fail_detail = error::format_server_failure(pull_failure_meta);
                    set_failure_state(boltprotocol::BoltError::UNKNOWN_ERROR, "Server FAILURE during PULL (buffering): " + server_fail_detail, pull_failure_meta);
                }
                co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
            } else {
                set_failure_state(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Unexpected tag " + std::to_string(static_cast<int>(tag)) + " during PULL (buffering)");
                co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, false);
            }
        }

        if (!raw_record_buffer_.empty()) {
            co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", true);
        }
        if (!server_has_more_records_after_last_pull_.load(std::memory_order_acquire)) {
            stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);
            co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", false);
        }
        co_return std::make_tuple(boltprotocol::BoltError::UNKNOWN_ERROR, "ensure_records_buffered_async: Inconsistent state after PULL.", false);
    }

    // --- next_async (from previous batch, user has corrected version checks) ---
    boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>>> AsyncResultStream::next_async() {
        auto [err_code, err_msg, has_more_locally] = co_await ensure_records_buffered_async();

        if (err_code != boltprotocol::BoltError::SUCCESS) {
            co_return std::make_tuple(err_code, std::move(err_msg), std::nullopt);
        }
        if (!has_more_locally) {
            co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "No more records in stream.", std::nullopt);
        }

        boltprotocol::RecordMessageParams raw_record_params = std::move(raw_record_buffer_.front());
        raw_record_buffer_.pop_front();

        BoltRecord record(std::move(raw_record_params.fields), field_names_ptr_cache_);
        co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", std::make_optional(std::move(record)));
    }

    // --- AsyncResultStream Private Helper: send_discard_async ---
    boost::asio::awaitable<boltprotocol::BoltError> AsyncResultStream::send_discard_async(int64_t n, boltprotocol::SuccessMessageParams& out_discard_summary_raw) {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
            logger = owner_session_->transport_manager_->get_config().logger;
        }

        if (!is_open()) {
            if (logger) logger->warn("[AsyncResultStream {}] send_discard_async on non-open stream.", (void*)this);
            co_return failure_reason_.load(std::memory_order_acquire);
        }

        boltprotocol::DiscardMessageParams discard_p;
        discard_p.n = n;
        if (query_id_.has_value() && (stream_context_->negotiated_bolt_version.major >= 4)) {
            discard_p.qid = query_id_;
        }

        std::vector<uint8_t> discard_payload_bytes;
        boltprotocol::PackStreamWriter writer(discard_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_discard_message(discard_p, writer);
        if (err != boltprotocol::BoltError::SUCCESS) {
            set_failure_state(err, "Failed to serialize DISCARD message: " + error::bolt_error_to_string(err));
            co_return err;  // Return the specific serialization error
        }

        if (logger) logger->trace("[AsyncResultStream {}] Sending DISCARD (n={}, qid={})", (void*)this, n, query_id_ ? std::to_string(*query_id_) : "auto");

        auto static_op_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            // This sets the stream's failure state if the static operation itself fails
            this->set_failure_state(reason, "Async DISCARD operation error: " + message);
        };

        auto [summary_err, discard_result_summary_obj] =  // discard_result_summary_obj is ResultSummary
            co_await internal::BoltPhysicalConnection::send_request_receive_summary_async_static(*stream_context_, discard_payload_bytes, stream_context_->original_config, logger, static_op_error_handler);

        if (summary_err != boltprotocol::BoltError::SUCCESS) {
            // If send_request_receive_summary_async_static returned an error,
            // set_failure_state should have been called by static_op_error_handler,
            // or if the error was directly from send_request_receive_summary_async_static (e.g. initial send failed),
            // we need to ensure failure state is set.
            if (!stream_failed_.load(std::memory_order_acquire)) {  // Check if handler already set it
                                                                    // If the ResultSummary object contains a server failure message (because summary_err might be generic like UNKNOWN_ERROR for server failures)
                                                                    // we use the message stored by set_failure_state (which should have been called by handler)
                                                                    // or construct a new one.
                std::string detail_msg = failure_message_;          // Use already set message if any
                if (detail_msg.empty()) {
                    detail_msg = "DISCARD request failed to get summary. Error code: " + std::to_string(static_cast<int>(summary_err));
                }
                set_failure_state(summary_err, detail_msg);
            }
            co_return failure_reason_.load(std::memory_order_acquire);
        }

        out_discard_summary_raw = discard_result_summary_obj.raw_params();
        co_return boltprotocol::BoltError::SUCCESS;
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
            if (logger) logger->trace("[AsyncResultStream {}] consume_async: No records on server to discard.", (void*)this);
            co_return std::make_pair(boltprotocol::BoltError::SUCCESS, final_summary_typed_);
        }

        boltprotocol::SuccessMessageParams discard_summary_raw;
        boltprotocol::BoltError discard_op_err = co_await send_discard_async(-1, discard_summary_raw);

        is_first_fetch_attempt_ = false;
        stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);

        if (discard_op_err != boltprotocol::BoltError::SUCCESS) {
            // failure_reason_ and failure_message_ should be set by send_discard_async or its handler
            co_return std::make_pair(failure_reason_.load(std::memory_order_acquire), final_summary_typed_);
        }

        update_final_summary(std::move(discard_summary_raw));
        server_has_more_records_after_last_pull_.store(false, std::memory_order_release);

        if (logger) logger->trace("[AsyncResultStream {}] consume_async successful.", (void*)this);
        co_return std::make_pair(boltprotocol::BoltError::SUCCESS, final_summary_typed_);
    }

}  // namespace neo4j_bolt_transport