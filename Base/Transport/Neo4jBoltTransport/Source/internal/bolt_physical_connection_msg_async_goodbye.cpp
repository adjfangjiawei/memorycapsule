#include <vector>

#include "boltprotocol/message_serialization.h"         // For serialize_goodbye_message
#include "boltprotocol/packstream_writer.h"             // For PackStreamWriter
#include "neo4j_bolt_transport/internal/async_types.h"  // For ActiveAsyncStreamContext
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {
        namespace detail_async_messaging_helpers {
            // Forward declare static helpers if they are in a different cpp
            boost::asio::awaitable<boltprotocol::BoltError> _send_chunked_payload_async_static_helper(
                internal::ActiveAsyncStreamContext& stream_ctx, std::vector<uint8_t> payload, const BoltConnectionConfig& conn_config_ref, std::shared_ptr<spdlog::logger> logger_ref, std::function<void(boltprotocol::BoltError, const std::string&)> error_handler);
        }  // namespace detail_async_messaging_helpers

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::send_goodbye_async_static(internal::ActiveAsyncStreamContext& stream_ctx,
                                                                                                          const BoltConnectionConfig& conn_config_ref,
                                                                                                          std::shared_ptr<spdlog::logger> logger_ref,
                                                                                                          std::function<void(boltprotocol::BoltError, const std::string&)> error_handler) {
            if (logger_ref) logger_ref->trace("[ConnMsgAsyncStatic] send_goodbye_async_static called.");

            std::vector<uint8_t> goodbye_payload;
            boltprotocol::PackStreamWriter writer(goodbye_payload);
            boltprotocol::BoltError err = boltprotocol::serialize_goodbye_message(writer);
            if (err != boltprotocol::BoltError::SUCCESS) {
                if (error_handler) error_handler(err, "Async Static: GOODBYE serialization failed.");
                co_return err;
            }

            err = co_await detail_async_messaging_helpers::_send_chunked_payload_async_static_helper(stream_ctx, std::move(goodbye_payload), conn_config_ref, logger_ref, error_handler);
            // No response expected for GOODBYE.
            co_return err;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport