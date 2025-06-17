#ifndef NEO4J_BOLT_TRANSPORT_URI_PARSED_URI_H
#define NEO4J_BOLT_TRANSPORT_URI_PARSED_URI_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace neo4j_bolt_transport {
    namespace uri {

        // Holds the deconstructed parts of a Neo4j connection URI.
        struct ParsedUri {
            std::string input_uri;
            std::string scheme;

            // For non-routing schemes, hosts will contain one entry.
            // For routing schemes (neo4j://, neo4j+s://), hosts can contain multiple seed router addresses.
            std::vector<std::pair<std::string, uint16_t>> hosts_with_ports;

            // Userinfo extracted from URI (if present)
            std::optional<std::string> username_from_uri;
            std::optional<std::string> password_from_uri;

            // Query parameters from the URI
            std::map<std::string, std::string> query_parameters;

            // Interpretation of the scheme
            bool is_valid = false;
            bool is_routing_scheme = false;
            bool tls_enabled_by_scheme = false;
            enum class SchemeTrustStrategy {
                NONE,            // e.g. bolt, neo4j (depends on server default or further config)
                SYSTEM_CAS,      // e.g. bolt+s, neo4j+s
                TRUST_ALL_CERTS  // e.g. bolt+ssc, neo4j+ssc (discouraged)
            };
            SchemeTrustStrategy trust_strategy_hint = SchemeTrustStrategy::NONE;

            // Standard default ports
            static constexpr uint16_t DEFAULT_BOLT_PORT = 7687;
            // Neo4j typically uses the same port for TLS-enabled Bolt (server-side config)
            static constexpr uint16_t DEFAULT_BOLTS_PORT = 7687;
        };

    }  // namespace uri
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_URI_PARSED_URI_H