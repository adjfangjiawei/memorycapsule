#ifndef NEO4J_BOLT_TRANSPORT_CONFIG_TRANSPORT_CONFIG_H
#define NEO4J_BOLT_TRANSPORT_CONFIG_TRANSPORT_CONFIG_H

#include <cstdint>
#include <functional>
#include <memory>  // For std::shared_ptr
#include <optional>
#include <string>
#include <vector>

#include "auth_token.h"
#include "boltprotocol/message_defs.h"
#include "spdlog/sinks/stdout_color_sinks.h"  // Example sink
#include "spdlog/spdlog.h"                    // <<< Include spdlog

namespace neo4j_bolt_transport {

    namespace uri {
        struct ParsedUri;
    }

    namespace config {

        struct TransportConfig {
            // ... (other members as before) ...
            std::string uri_string;
            AuthTokenVariant auth_token = AuthTokens::none();
            std::string user_agent_override;
            boltprotocol::HelloMessageParams::BoltAgentInfo bolt_agent_info;

            enum class EncryptionStrategy { /* ... */ NEGOTIATE_FROM_URI_SCHEME, FORCE_PLAINTEXT, FORCE_ENCRYPTED_SYSTEM_CERTS, FORCE_ENCRYPTED_TRUST_ALL_CERTS, FORCE_ENCRYPTED_CUSTOM_CERTS };
            EncryptionStrategy encryption_strategy = EncryptionStrategy::NEGOTIATE_FROM_URI_SCHEME;
            std::vector<std::string> trusted_certificates_pem_files;
            std::optional<std::string> client_certificate_pem_file;
            std::optional<std::string> client_private_key_pem_file;
            std::optional<std::string> client_private_key_password;
            bool hostname_verification_enabled = true;

            std::size_t max_connection_pool_size = 100;
            uint32_t connection_acquisition_timeout_ms = 60000;
            uint32_t max_connection_lifetime_ms = 3600000;
            uint32_t idle_timeout_ms = 600000;
            uint32_t idle_time_before_health_check_ms = 30000;

            uint32_t tcp_connect_timeout_ms = 5000;
            bool tcp_keep_alive_enabled = true;
            uint32_t max_transaction_retry_time_ms = 30000;
            uint32_t transaction_retry_delay_initial_ms = 1000;
            uint32_t transaction_retry_delay_multiplier = 2;
            uint32_t transaction_retry_delay_max_ms = 60000;

            bool client_side_routing_enabled = true;
            uint32_t routing_table_refresh_ttl_margin_ms = 5000;
            uint32_t routing_max_retry_attempts = 3;

            // --- Logging ---
            std::shared_ptr<spdlog::logger> logger;                     // User can provide a logger
            spdlog::level::level_enum log_level = spdlog::level::info;  // Default log level

            TransportConfig(const std::string& uri_str);
            TransportConfig();

            boltprotocol::BoltError apply_parsed_uri_settings(const uri::ParsedUri& parsed_uri);
            void prepare_agent_strings(const std::string& default_transport_name_version = "Neo4jBoltTransportCpp/0.4.0");  // Version bump

            // Helper to get or create a default logger
            std::shared_ptr<spdlog::logger> get_or_create_logger(const std::string& logger_name = "Neo4jBoltTransport");
        };

    }  // namespace config
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_CONFIG_TRANSPORT_CONFIG_H