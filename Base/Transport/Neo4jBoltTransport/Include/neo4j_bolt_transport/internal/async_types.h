#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_TYPES_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_TYPES_H

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <memory>  // For std::weak_ptr
#include <string>
#include <variant>

#include "boltprotocol/message_defs.h"  // For versions::Version

namespace neo4j_bolt_transport {
    namespace internal {

        // Represents an established, active asynchronous stream and its parameters.
        // Ownership of the stream object is held by this context.
        struct ActiveAsyncStreamContext {
            std::variant<boost::asio::ip::tcp::socket, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> stream;

            boltprotocol::versions::Version negotiated_bolt_version;
            std::string server_agent_string;
            std::string server_connection_id;
            bool utc_patch_active = false;
            bool encryption_was_used = false;

            ActiveAsyncStreamContext(std::variant<boost::asio::ip::tcp::socket, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> s, boltprotocol::versions::Version bv, std::string sa, std::string cid, bool utc, bool enc_used)
                : stream(std::move(s)), negotiated_bolt_version(bv), server_agent_string(std::move(sa)), server_connection_id(std::move(cid)), utc_patch_active(utc), encryption_was_used(enc_used) {
            }

            explicit ActiveAsyncStreamContext(boost::asio::io_context& ioc) : stream(boost::asio::ip::tcp::socket(ioc)) {
            }
            ActiveAsyncStreamContext() = delete;

            ActiveAsyncStreamContext(const ActiveAsyncStreamContext&) = delete;
            ActiveAsyncStreamContext& operator=(const ActiveAsyncStreamContext&) = delete;
            ActiveAsyncStreamContext(ActiveAsyncStreamContext&&) = default;
            ActiveAsyncStreamContext& operator=(ActiveAsyncStreamContext&&) = default;

            boost::asio::any_io_executor get_executor() {
                return std::visit(
                    [](auto& s) {
                        return s.get_executor();
                    },
                    stream);
            }
        };

    }  // namespace internal
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_TYPES_H