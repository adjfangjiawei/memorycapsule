#ifndef NEO4J_BOLT_TRANSPORT_CONFIG_TRANSPORT_CONFIG_H
#define NEO4J_BOLT_TRANSPORT_CONFIG_TRANSPORT_CONFIG_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>  // For set of ServerAddress
#include <string>
#include <vector>

#include "auth_token.h"
#include "boltprotocol/message_defs.h"
#include "neo4j_bolt_transport/routing/server_address.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

namespace neo4j_bolt_transport {

    namespace uri {
        struct ParsedUri;
    }

    namespace config {

        struct TransportConfig {
            std::string uri_string;
            AuthTokenVariant auth_token = AuthTokens::none();
            std::string user_agent_override;
            boltprotocol::HelloMessageParams::BoltAgentInfo bolt_agent_info;

            enum class EncryptionStrategy { NEGOTIATE_FROM_URI_SCHEME, FORCE_PLAINTEXT, FORCE_ENCRYPTED_SYSTEM_CERTS, FORCE_ENCRYPTED_TRUST_ALL_CERTS, FORCE_ENCRYPTED_CUSTOM_CERTS };
            EncryptionStrategy encryption_strategy = EncryptionStrategy::NEGOTIATE_FROM_URI_SCHEME;
            std::vector<std::string> trusted_certificates_pem_files;
            std::optional<std::string> client_certificate_pem_file;
            std::optional<std::string> client_private_key_pem_file;
            std::optional<std::string> client_private_key_password;
            bool hostname_verification_enabled = true;

            std::size_t max_connection_pool_size = 100;
            uint32_t connection_acquisition_timeout_ms = 60000;  // Timeout for acquiring a connection from the pool
            uint32_t max_connection_lifetime_ms = 3600000;       // Max lifetime of a pooled connection
            uint32_t idle_timeout_ms = 600000;                   // Max idle time for a pooled connection
            uint32_t idle_time_before_health_check_ms = 30000;   // Idle time after which a health check (ping) is performed before reuse

            // Socket level timeouts
            uint32_t tcp_connect_timeout_ms = 5000;  // Timeout for establishing the TCP connection
            uint32_t socket_read_timeout_ms = 0;     // Timeout for socket read operations (0 = system default/infinite)
            uint32_t socket_write_timeout_ms = 0;    // Timeout for socket write operations (0 = system default/infinite)
            bool tcp_keep_alive_enabled = true;
            bool tcp_no_delay_enabled = true;

            // Bolt protocol level timeouts
            uint32_t hello_timeout_ms = 15000;   // Timeout for HELLO message exchange
            uint32_t goodbye_timeout_ms = 5000;  // Timeout for GOODBYE message exchange (if sent)

            // Transaction related configurations
            uint32_t max_transaction_retry_time_ms = 30000;  // Max total time for retrying a managed transaction
            uint32_t transaction_retry_delay_initial_ms = 1000;
            uint32_t transaction_retry_delay_multiplier = 2;
            uint32_t transaction_retry_delay_max_ms = 60000;
            uint32_t explicit_transaction_timeout_default_ms = 0;  // Default timeout for explicit transactions if not specified per-transaction (0 = server default)

            // --- Routing ---
            bool client_side_routing_enabled = true;
            uint32_t routing_table_refresh_ttl_margin_ms = 5000;
            uint32_t routing_max_retry_attempts = 3;
            std::function<routing::ServerAddress(const routing::ServerAddress&)> server_address_resolver;
            std::map<std::string, std::vector<routing::ServerAddress>> initial_router_addresses_override;

            // --- Bolt Protocol ---
            std::vector<boltprotocol::versions::Version> preferred_bolt_versions;

            // --- Logging ---
            std::shared_ptr<spdlog::logger> logger;
            spdlog::level::level_enum log_level = spdlog::level::info;

            TransportConfig(const std::string& uri_str = "bolt://localhost:7687");
            TransportConfig();

            boltprotocol::BoltError apply_parsed_uri_settings(const uri::ParsedUri& parsed_uri);
            void prepare_agent_strings(const std::string& default_transport_name_version = "Neo4jBoltTransportCpp/0.6.0");  // Version bump

            std::shared_ptr<spdlog::logger> get_or_create_logger(const std::string& logger_name = "Neo4jBoltTransport");
        };

    }  // namespace config
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_CONFIG_TRANSPORT_CONFIG_H