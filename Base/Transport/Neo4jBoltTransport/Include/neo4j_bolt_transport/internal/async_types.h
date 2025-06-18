#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_TYPES_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_TYPES_H

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <memory>  // For std::unique_ptr, std::shared_ptr (though not directly here)
#include <string>
#include <variant>

#include "boltprotocol/message_defs.h"                             // For versions::Version
#include "neo4j_bolt_transport/internal/bolt_connection_config.h"  // For BoltConnectionConfig

namespace neo4j_bolt_transport {
    namespace internal {

        // Represents an established, active asynchronous stream, its parameters, and its original configuration.
        // Ownership of the stream object is typically held by this context when returned from an establishment function.
        struct ActiveAsyncStreamContext {
            // The actual I/O stream variant (plain TCP socket or SSL stream over TCP socket)
            std::variant<boost::asio::ip::tcp::socket, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> stream;

            // The specific BoltConnectionConfig instance that was used to establish this stream.
            // This is crucial for operations on this stream that need precise configuration details (e.g., timeouts for GOODBYE).
            BoltConnectionConfig original_config;

            // Bolt protocol version negotiated on this stream.
            boltprotocol::versions::Version negotiated_bolt_version;
            // Server agent string obtained from HELLO response.
            std::string server_agent_string;
            // Connection ID assigned by the server, obtained from HELLO/LOGON response.
            std::string server_connection_id;
            // Flag indicating if the "utc" patch (for modern DateTime/DateTimeZoneId) is active.
            bool utc_patch_active = false;
            // Flag indicating if TLS encryption was actually used for this connection.
            bool encryption_was_used = false;

            // Constructor for a fully established and configured context.
            ActiveAsyncStreamContext(std::variant<boost::asio::ip::tcp::socket, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> s,  // The stream itself
                                     BoltConnectionConfig config,                                                                           // The config used to create this stream
                                     boltprotocol::versions::Version bv,                                                                    // Negotiated Bolt version
                                     std::string sa,                                                                                        // Server agent
                                     std::string cid,                                                                                       // Connection ID
                                     bool utc,                                                                                              // UTC patch status
                                     bool enc_used                                                                                          // Encryption status
                                     )
                : stream(std::move(s)),
                  original_config(std::move(config)),  // Store the specific configuration
                  negotiated_bolt_version(bv),
                  server_agent_string(std::move(sa)),
                  server_connection_id(std::move(cid)),
                  utc_patch_active(utc),
                  encryption_was_used(enc_used) {
            }

            // Constructor used as a placeholder or for partially initialized states,
            // typically when the stream is created but not yet fully configured/handshaken.
            // The `original_config` will be default-constructed here; ensure BoltConnectionConfig's
            // default constructor is meaningful or that this config is properly populated later if used.
            explicit ActiveAsyncStreamContext(boost::asio::io_context& ioc)
                : stream(boost::asio::ip::tcp::socket(ioc)),
                  original_config()  // Default constructs BoltConnectionConfig
            {
            }

            // Deleted default constructor to force initialization with necessary components.
            ActiveAsyncStreamContext() = delete;

            // Delete copy operations to prevent accidental copying of unique stream resources.
            ActiveAsyncStreamContext(const ActiveAsyncStreamContext&) = delete;
            ActiveAsyncStreamContext& operator=(const ActiveAsyncStreamContext&) = delete;

            // Defaulted move operations are suitable as `stream` and `original_config` are movable.
            ActiveAsyncStreamContext(ActiveAsyncStreamContext&&) = default;
            ActiveAsyncStreamContext& operator=(ActiveAsyncStreamContext&&) = default;

            // Provides access to the executor associated with the underlying stream.
            boost::asio::any_io_executor get_executor() {
                return std::visit(
                    // Lambda takes a reference to the variant's alternative
                    [](auto& s_ref) {
                        return s_ref.get_executor();
                    },
                    stream);  // Pass the stream variant to std::visit
            }
        };

    }  // namespace internal
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_TYPES_H