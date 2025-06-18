#include <vector>

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/internal/async_types.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

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

            err = co_await BoltPhysicalConnection::_send_chunked_payload_async_static_helper(  // Call static member
                stream_ctx,
                std::move(goodbye_payload),
                conn_config_ref,
                logger_ref,
                error_handler);
            co_return err;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport