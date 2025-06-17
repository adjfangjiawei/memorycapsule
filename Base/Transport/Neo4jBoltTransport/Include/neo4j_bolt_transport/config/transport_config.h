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
#include "neo4j_bolt_transport/routing/server_address.h"  // <--- NEW
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
            uint32_t connection_acquisition_timeout_ms = 60000;
            uint32_t max_connection_lifetime_ms = 3600000;
            uint32_t idle_timeout_ms = 600000;  // Renamed from idle_connection_timeout_ms for consistency
            uint32_t idle_time_before_health_check_ms = 30000;

            uint32_t tcp_connect_timeout_ms = 5000;
            bool tcp_keep_alive_enabled = true;
            bool tcp_no_delay_enabled = true;  // <--- NEW (TCP_NODELAY)

            uint32_t max_transaction_retry_time_ms = 30000;
            uint32_t transaction_retry_delay_initial_ms = 1000;
            uint32_t transaction_retry_delay_multiplier = 2;  // Keep as int, cast to double if needed
            uint32_t transaction_retry_delay_max_ms = 60000;

            // --- Routing ---
            bool client_side_routing_enabled = true;  // Default to true if scheme allows
            uint32_t routing_table_refresh_ttl_margin_ms = 5000;
            uint32_t routing_max_retry_attempts = 3;                                                       // Retries for fetching routing table
            std::function<routing::ServerAddress(const routing::ServerAddress&)> server_address_resolver;  // <--- NEW (Custom resolver)
            // Map of initial router addresses. Keyed by context (e.g. initial URI authority)
            // This allows different sets of initial routers if the driver instance is used for multiple clusters.
            // For simpler single-cluster use, it might just contain one entry.
            std::map<std::string, std::vector<routing::ServerAddress>> initial_router_addresses_override;  // <--- NEW (Advanced: To override URI parsing for initial routers)

            // --- Bolt Protocol ---
            std::vector<boltprotocol::versions::Version> preferred_bolt_versions;  // <--- NEW

            // --- Logging ---
            std::shared_ptr<spdlog::logger> logger;
            spdlog::level::level_enum log_level = spdlog::level::info;

            TransportConfig(const std::string& uri_str = "bolt://localhost:7687");  // Default constructor if no URI
            TransportConfig();                                                      // Explicit default constructor

            boltprotocol::BoltError apply_parsed_uri_settings(const uri::ParsedUri& parsed_uri);
            void prepare_agent_strings(const std::string& default_transport_name_version = "Neo4jBoltTransportCpp/0.5.0");  // Version bump

            std::shared_ptr<spdlog::logger> get_or_create_logger(const std::string& logger_name = "Neo4jBoltTransport");

            // Builder pattern might be useful here eventually
        };

    }  // namespace config
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_CONFIG_TRANSPORT_CONFIG_H