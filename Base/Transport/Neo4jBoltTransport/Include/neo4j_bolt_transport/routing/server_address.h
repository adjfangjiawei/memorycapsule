#ifndef NEO4J_BOLT_TRANSPORT_ROUTING_SERVER_ADDRESS_H
#define NEO4J_BOLT_TRANSPORT_ROUTING_SERVER_ADDRESS_H

#include <cstdint>
#include <functional>  // For std::hash
#include <string>

namespace neo4j_bolt_transport {
    namespace routing {

        struct ServerAddress {
            std::string host;
            std::uint16_t port;

            ServerAddress(std::string h = "", std::uint16_t p = 0) : host(std::move(h)), port(p) {
            }

            bool operator==(const ServerAddress& other) const {
                return host == other.host && port == other.port;
            }

            bool operator<(const ServerAddress& other) const {
                if (host != other.host) {
                    return host < other.host;
                }
                return port < other.port;
            }

            std::string to_string() const {
                return host + ":" + std::to_string(port);
            }
        };

    }  // namespace routing
}  // namespace neo4j_bolt_transport

namespace std {
    template <>
    struct hash<neo4j_bolt_transport::routing::ServerAddress> {
        size_t operator()(const neo4j_bolt_transport::routing::ServerAddress& addr) const noexcept {
            size_t h1 = std::hash<std::string>{}(addr.host);
            size_t h2 = std::hash<std::uint16_t>{}(addr.port);
            return h1 ^ (h2 << 1);  // Basic combination
        }
    };
}  // namespace std

#endif  // NEO4J_BOLT_TRANSPORT_ROUTING_SERVER_ADDRESS_H