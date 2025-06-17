#include "neo4j_bolt_transport/config/transport_config.h"

#include <iostream>  // Added for std::cerr

#include "neo4j_bolt_transport/uri/parsed_uri.h"
#include "neo4j_bolt_transport/uri/uri_parser.h"
#include "spdlog/sinks/basic_file_sink.h"     // For default file logger (example)
#include "spdlog/sinks/stdout_color_sinks.h"  // For default console logger

namespace neo4j_bolt_transport {
    namespace config {

        TransportConfig::TransportConfig(const std::string& a_uri_string) : uri_string(a_uri_string) {
            uri::ParsedUri parsed_uri_info;
            // Parse URI and apply its settings (host, port, encryption hint)
            if (uri::UriParser::parse(uri_string, parsed_uri_info) == boltprotocol::BoltError::SUCCESS) {
                apply_parsed_uri_settings(parsed_uri_info);  // This populates some config_ fields
            } else {
                // If URI parsing fails, what should be the default state?
                // Maybe use localhost and default port, but mark config as potentially invalid
                // or rely on user to have set host/port directly if URI is bad.
                // For now, if URI is bad, subsequent connection attempts will likely fail due to bad host/port.
                // A robust constructor might throw here or have a "valid" flag.
            }
            prepare_agent_strings();
            logger = get_or_create_logger();  // Initialize with default logger if not set
        }

        TransportConfig::TransportConfig() : uri_string("bolt://localhost:7687") {  // Default URI if none provided
            uri::ParsedUri parsed_uri_info;
            if (uri::UriParser::parse(uri_string, parsed_uri_info) == boltprotocol::BoltError::SUCCESS) {
                apply_parsed_uri_settings(parsed_uri_info);
            }
            prepare_agent_strings();
            logger = get_or_create_logger();
        }

        boltprotocol::BoltError TransportConfig::apply_parsed_uri_settings(const uri::ParsedUri& parsed_uri) {
            if (!parsed_uri.is_valid) {
                return boltprotocol::BoltError::INVALID_ARGUMENT;
            }

            // URI parsing sets a hint for encryption; user can override with specific FORCE_ strategy
            if (encryption_strategy == EncryptionStrategy::NEGOTIATE_FROM_URI_SCHEME) {
                if (parsed_uri.tls_enabled_by_scheme) {
                    if (parsed_uri.trust_strategy_hint == uri::ParsedUri::SchemeTrustStrategy::SYSTEM_CAS) {
                        encryption_strategy = EncryptionStrategy::FORCE_ENCRYPTED_SYSTEM_CERTS;
                    } else if (parsed_uri.trust_strategy_hint == uri::ParsedUri::SchemeTrustStrategy::TRUST_ALL_CERTS) {
                        encryption_strategy = EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS;
                    } else {  // Includes NONE if tls_enabled_by_scheme but no specific trust hint
                        encryption_strategy = EncryptionStrategy::FORCE_ENCRYPTED_SYSTEM_CERTS;
                    }
                } else {
                    encryption_strategy = EncryptionStrategy::FORCE_PLAINTEXT;
                }
            }

            if (parsed_uri.is_routing_scheme) {
                // client_side_routing_enabled is true by default if scheme allows, user can set to false.
            } else {
                client_side_routing_enabled = false;
            }

            // Auth token from URI (if not already set by user directly on TransportConfig)
            if (std::holds_alternative<NoAuth>(auth_token)) {
                if (parsed_uri.username_from_uri.has_value()) {
                    auth_token = AuthTokens::basic(parsed_uri.username_from_uri.value(), parsed_uri.password_from_uri.value_or(""), std::nullopt);
                }
            }
            // Query parameters could override other settings here.
            // For example, ?connection_timeout=10000
            auto it_conn_timeout = parsed_uri.query_parameters.find("connection_timeout");
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
                // TODO: Populate platform, language, etc. if possible/desired
                // bolt_agent_info.platform = "DetectedPlatform";
                // bolt_agent_info.language = "C++20"; // Or detected compiler
            }

            // If user_agent_override is set, it's used as is.
            // Otherwise, Neo4jBoltTransport will construct a default one using finalized_bolt_agent_info.
        }

        std::shared_ptr<spdlog::logger> TransportConfig::get_or_create_logger(const std::string& logger_name) {
            if (logger) {
                return logger;
            }
            // Try to get existing logger, if not, create a default one
            auto default_logger = spdlog::get(logger_name);
            if (!default_logger) {
                try {
                    // Example: create a console logger. Could also be file logger, etc.
                    default_logger = spdlog::stdout_color_mt(logger_name);
                    // Set a default pattern and level for the newly created logger
                    default_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] [%t] %v");
                    default_logger->set_level(log_level);  // Use configured level
                    // spdlog::flush_every(std::chrono::seconds(3)); // Example: flush periodically
                } catch (const spdlog::spdlog_ex& ex) {
                    // Fallback to a very basic logger if creation fails (e.g. name conflict in rare cases)
                    std::cerr << "Logger initialization failed: " << ex.what() << std::endl;  // std::cerr used here
                    // Could return a null logger or a simple ostream logger.
                    // For simplicity, we'll let it be null if spdlog setup fails.
                    return nullptr;
                }
            } else {
                default_logger->set_level(log_level);  // Ensure existing logger has the configured level
            }
            return default_logger;
        }

    }  // namespace config
}  // namespace neo4j_bolt_transport