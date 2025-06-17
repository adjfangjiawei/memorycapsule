#include "neo4j_bolt_transport/config/transport_config.h"

#include <iostream>

#include "boltprotocol/bolt_errors_versions.h"  // For default versions
#include "neo4j_bolt_transport/uri/parsed_uri.h"
#include "neo4j_bolt_transport/uri/uri_parser.h"

namespace neo4j_bolt_transport {
    namespace config {

        TransportConfig::TransportConfig(const std::string& a_uri_string) : uri_string(a_uri_string.empty() ? "bolt://localhost:7687" : a_uri_string) {
            uri::ParsedUri parsed_uri_info;
            if (uri::UriParser::parse(uri_string, parsed_uri_info) == boltprotocol::BoltError::SUCCESS) {
                apply_parsed_uri_settings(parsed_uri_info);
            } else {
                // Log error or handle gracefully if URI parsing fails
                // For now, rely on logger if initialized, otherwise perhaps std::cerr
                // This state might leave config partially initialized if URI is critical.
                if (logger) {
                    logger->error("Failed to parse URI '{}' during TransportConfig construction. Using defaults where possible.", uri_string);
                } else {
                    std::cerr << "Error: Failed to parse URI '" << uri_string << "' during TransportConfig construction." << std::endl;
                }
            }
            // Set default preferred Bolt versions if not already set
            if (preferred_bolt_versions.empty()) {
                preferred_bolt_versions = boltprotocol::versions::get_default_proposed_versions();
            }
            prepare_agent_strings();
            logger = get_or_create_logger();
        }

        TransportConfig::TransportConfig() : TransportConfig("bolt://localhost:7687") {
        }

        boltprotocol::BoltError TransportConfig::apply_parsed_uri_settings(const uri::ParsedUri& parsed_uri) {
            if (!parsed_uri.is_valid) {
                return boltprotocol::BoltError::INVALID_ARGUMENT;
            }

            // Encryption strategy from URI (can be overridden by direct config)
            if (encryption_strategy == EncryptionStrategy::NEGOTIATE_FROM_URI_SCHEME) {
                if (parsed_uri.tls_enabled_by_scheme) {
                    if (parsed_uri.trust_strategy_hint == uri::ParsedUri::SchemeTrustStrategy::SYSTEM_CAS) {
                        encryption_strategy = EncryptionStrategy::FORCE_ENCRYPTED_SYSTEM_CERTS;
                    } else if (parsed_uri.trust_strategy_hint == uri::ParsedUri::SchemeTrustStrategy::TRUST_ALL_CERTS) {
                        encryption_strategy = EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS;
                    } else {
                        encryption_strategy = EncryptionStrategy::FORCE_ENCRYPTED_SYSTEM_CERTS;  // Default for +s/+ssc without specific override
                    }
                } else {
                    encryption_strategy = EncryptionStrategy::FORCE_PLAINTEXT;
                }
            }

            // Routing enablement based on scheme (can be overridden by direct config)
            if (!parsed_uri.is_routing_scheme) {
                client_side_routing_enabled = false;
            }
            // If client_side_routing_enabled was false, and scheme is routing, it stays false.
            // If it was true (default), and scheme is routing, it stays true.

            // Auth token from URI if not already set by user
            if (std::holds_alternative<NoAuth>(auth_token)) {
                if (parsed_uri.username_from_uri.has_value()) {
                    auth_token = AuthTokens::basic(parsed_uri.username_from_uri.value(), parsed_uri.password_from_uri.value_or(""), std::nullopt);
                }
            }

            // Initial routers from URI (if not overridden by initial_router_addresses_override)
            if (client_side_routing_enabled && initial_router_addresses_override.empty() && !parsed_uri.hosts_with_ports.empty()) {
                std::string initial_context_key = parsed_uri.scheme + "://" + parsed_uri.hosts_with_ports.front().first;  // Simplified context key
                std::vector<routing::ServerAddress> initial_routers;
                for (const auto& host_port_pair : parsed_uri.hosts_with_ports) {
                    initial_routers.emplace_back(host_port_pair.first, host_port_pair.second);
                }
                initial_router_addresses_override[initial_context_key] = initial_routers;
            }

            // Example: Query parameters overriding config
            auto it_conn_timeout = parsed_uri.query_parameters.find("connection_timeout_ms");  // More specific name
            if (it_conn_timeout != parsed_uri.query_parameters.end()) {
                try {
                    tcp_connect_timeout_ms = static_cast<uint32_t>(std::stoul(it_conn_timeout->second));
                } catch (const std::exception&) { /* ignore invalid param */
                }
            }
            // ... parse other relevant query params ...

            return boltprotocol::BoltError::SUCCESS;
        }

        void TransportConfig::prepare_agent_strings(const std::string& default_transport_name_version) {
            if (bolt_agent_info.product.empty()) {
                bolt_agent_info.product = default_transport_name_version;
                // Consider populating platform, language, etc.
                // bolt_agent_info.platform = "OS_INFO"; // e.g., from preprocessor macros
                // bolt_agent_info.language = "C++20";
            }
        }

        std::shared_ptr<spdlog::logger> TransportConfig::get_or_create_logger(const std::string& logger_name) {
            if (logger) {
                logger->set_level(log_level);  // Ensure existing logger has current level
                return logger;
            }
            auto default_logger = spdlog::get(logger_name);
            if (!default_logger) {
                try {
                    default_logger = spdlog::stdout_color_mt(logger_name);
                    default_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] [%t] %v");
                    default_logger->set_level(log_level);
                } catch (const spdlog::spdlog_ex& ex) {
                    std::cerr << "Logger (" << logger_name << ") initialization failed: " << ex.what() << std::endl;
                    return nullptr;
                }
            } else {
                default_logger->set_level(log_level);  // Ensure existing logger has current level
            }
            return default_logger;
        }

    }  // namespace config
}  // namespace neo4j_bolt_transport