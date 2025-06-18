#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_CONNECTION_CONFIG_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_CONNECTION_CONFIG_H

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "neo4j_bolt_transport/config/transport_config.h"

namespace neo4j_bolt_transport {
    namespace internal {

        struct BoltConnectionConfig {
            std::string target_host;
            uint16_t target_port = 0;  // Default port to 0

            config::AuthTokenVariant auth_token;  // Default constructs if AuthTokenVariant can
            std::string user_agent_for_hello;
            boltprotocol::HelloMessageParams::BoltAgentInfo bolt_agent_info_for_hello;

            bool encryption_enabled = false;
            config::TransportConfig::EncryptionStrategy resolved_encryption_strategy = config::TransportConfig::EncryptionStrategy::NEGOTIATE_FROM_URI_SCHEME;
            std::vector<std::string> trusted_certificates_pem_files;
            std::optional<std::string> client_certificate_pem_file;
            std::optional<std::string> client_private_key_pem_file;
            std::optional<std::string> client_private_key_password;
            bool hostname_verification_enabled = true;

            uint32_t tcp_connect_timeout_ms = 5000;
            uint32_t socket_read_timeout_ms = 0;
            uint32_t socket_write_timeout_ms = 0;
            bool socket_keep_alive_enabled = true;
            bool tcp_no_delay_enabled = true;
            uint32_t bolt_handshake_timeout_ms = 10000;
            uint32_t hello_timeout_ms = 15000;
            uint32_t goodbye_timeout_ms = 5000;

            std::optional<std::map<std::string, boltprotocol::Value>> hello_routing_context;
            std::optional<std::vector<boltprotocol::versions::Version>> preferred_bolt_versions;

            // Add a default constructor
            BoltConnectionConfig() = default;
        };

    }  // namespace internal
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_CONNECTION_CONFIG_H