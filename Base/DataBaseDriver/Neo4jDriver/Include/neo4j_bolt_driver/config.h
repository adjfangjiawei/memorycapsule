#ifndef NEO4J_BOLT_DRIVER_CONFIG_H
#define NEO4J_BOLT_DRIVER_CONFIG_H

#include <chrono>
#include <cstdint>  // For uint16_t port
#include <string>
#include <vector>  // For CA files list

namespace neo4j_bolt_driver {

    enum class AuthScheme {
        NONE,
        BASIC,
        KERBEROS,  // Example, Bolt supports pluggable auth
        CUSTOM     // For other schemes
    };

    struct TLSConfig {
        bool enabled = false;
        // Path to a single CA certificate file or a directory containing CA
        // certificates
        std::string trust_store_path = "";
        // If true, server certificate hostname will be verified against the host we
        // are connecting to.
        bool verify_hostname = true;
        // Optional: Client certificate and private key for mutual TLS
        std::string client_cert_path = "";
        std::string client_key_path = "";
    };

    struct BoltDriverConfig {
        std::string host = "localhost";
        uint16_t port = 7687;  // Default Bolt port

        AuthScheme auth_scheme = AuthScheme::BASIC;
        std::string username = "";
        std::string password = "";
        std::string realm = "";  // For Kerberos
        std::string user_agent = "neo4j-bolt-cpp-custom/0.1.0-dev";
        std::string database_name = "";  // Optional: specify database for Bolt >= 4.0. Empty for default.

        TLSConfig tls_config;

        std::chrono::milliseconds connection_timeout{30000};  // 30 seconds
        std::chrono::milliseconds socket_timeout{15000};      // 15 seconds for send/receive

        // BoltDriverConfig(); // Default constructor is fine
        // Explicit constructors if needed, but simple struct initialization is often
        // sufficient.
    };

}  // namespace neo4j_bolt_driver

#endif  // NEO4J_BOLT_DRIVER_CONFIG_H