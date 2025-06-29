#include <utility>  // For std::make_pair
#include <variant>
#include <vector>

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/async_types.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/result_summary.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> BoltPhysicalConnection::send_request_receive_summary_async_static(
            internal::ActiveAsyncStreamContext& stream_ctx, const std::vector<uint8_t>& request_payload, const BoltConnectionConfig& conn_config_ref, std::shared_ptr<spdlog::logger> logger_ref, std::function<void(boltprotocol::BoltError, const std::string&)> error_handler) {
            if (logger_ref)
                logger_ref->trace(
                    "[ConnMsgAsyncStatic] send_request_receive_summary_async_static "
                    "called.");

            ResultSummary default_summary_on_error(  // Construct default summary for error cases
                {},
                stream_ctx.negotiated_bolt_version,
                stream_ctx.utc_patch_active,
                conn_config_ref.target_host + ":" + std::to_string(conn_config_ref.target_port),
                std::nullopt  // No specific database name from session here
            );

            boltprotocol::BoltError err = co_await BoltPhysicalConnection::_send_chunked_payload_async_static_helper(stream_ctx, request_payload, conn_config_ref, logger_ref, error_handler);
            if (err != boltprotocol::BoltError::SUCCESS) {
                co_return std::make_pair(err, default_summary_on_error);
            }

            std::vector<uint8_t> response_payload;
            while (true) {
                auto [recv_err, current_payload] = co_await BoltPhysicalConnection::_receive_chunked_payload_async_static_helper(stream_ctx, conn_config_ref, logger_ref, error_handler);
                if (recv_err != boltprotocol::BoltError::SUCCESS) {
                    co_return std::make_pair(recv_err, default_summary_on_error);
                }
                if (!current_payload.empty()) {
                    response_payload = std::move(current_payload);
                    break;
                }
                if (logger_ref) logger_ref->trace("[ConnMsgAsyncStatic] Received NOOP while awaiting summary.");
            }

            boltprotocol::MessageTag tag;
            if (response_payload.empty() || response_payload.size() < 2) {
                if (error_handler) error_handler(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Async Static: Invalid/empty summary response payload for peek.");
                co_return std::make_pair(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, default_summary_on_error);
            }
            boltprotocol::PackStreamReader peek_reader(response_payload);
            uint8_t raw_tag_byte_peek = 0;
            uint32_t num_fields_peek = 0;
            boltprotocol::BoltError peek_err_code = boltprotocol::peek_message_structure_header(peek_reader, raw_tag_byte_peek, num_fields_peek);
            if (peek_err_code != boltprotocol::BoltError::SUCCESS) {
                if (error_handler) error_handler(peek_err_code, "Async Static: Failed to peek response tag.");
                co_return std::make_pair(peek_err_code, default_summary_on_error);
            }
            tag = static_cast<boltprotocol::MessageTag>(raw_tag_byte_peek);

            boltprotocol::PackStreamReader reader(response_payload);
            boltprotocol::SuccessMessageParams success_meta;
            boltprotocol::FailureMessageParams failure_meta;
            std::string server_addr_str = conn_config_ref.target_host + ":" + std::to_string(conn_config_ref.target_port);

            if (tag == boltprotocol::MessageTag::SUCCESS) {
                err = boltprotocol::deserialize_success_message(reader, success_meta);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    if (error_handler) error_handler(err, "Async Static: Failed to deserialize SUCCESS summary.");
                    co_return std::make_pair(err, default_summary_on_error);
                }
                co_return std::make_pair(boltprotocol::BoltError::SUCCESS, ResultSummary(std::move(success_meta), stream_ctx.negotiated_bolt_version, stream_ctx.utc_patch_active, server_addr_str, std::nullopt));
            } else if (tag == boltprotocol::MessageTag::FAILURE) {
                err = boltprotocol::deserialize_failure_message(reader, failure_meta);
                if (err != boltprotocol::BoltError::SUCCESS) {
                    if (error_handler) error_handler(err, "Async Static: Failed to deserialize FAILURE summary.");
                    co_return std::make_pair(err, default_summary_on_error);
                }
                std::string server_fail_detail = error::format_server_failure(failure_meta);
                if (error_handler) error_handler(boltprotocol::BoltError::UNKNOWN_ERROR, "Async Static: Server failure: " + server_fail_detail);
                co_return std::make_pair(boltprotocol::BoltError::UNKNOWN_ERROR, ResultSummary(boltprotocol::SuccessMessageParams{std::move(failure_meta.metadata)}, stream_ctx.negotiated_bolt_version, stream_ctx.utc_patch_active, server_addr_str, std::nullopt));
            } else {
                std::string unexpected_tag_msg = "Async Static: Unexpected summary tag " + std::to_string(static_cast<int>(tag));
                if (error_handler) error_handler(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, unexpected_tag_msg);
                co_return std::make_pair(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, default_summary_on_error);
            }
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport