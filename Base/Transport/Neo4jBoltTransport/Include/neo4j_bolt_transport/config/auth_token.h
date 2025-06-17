#ifndef NEO4J_BOLT_TRANSPORT_CONFIG_AUTH_TOKEN_H
#define NEO4J_BOLT_TRANSPORT_CONFIG_AUTH_TOKEN_H

#include <map>
#include <optional>
#include <string>
#include <variant>

#include "boltprotocol/message_defs.h"  // For boltprotocol::Value

namespace neo4j_bolt_transport {
    namespace config {

        struct NoAuth {};

        struct BasicAuth {
            std::string username;
            std::string password;
            std::optional<std::string> realm;
        };

        struct KerberosAuth {
            std::string base64_ticket;
        };

        struct CustomAuth {
            std::string principal;
            std::string credentials;
            std::optional<std::string> realm;
            std::string scheme;  // The custom scheme name, e.g., "custom_sso"
            std::optional<std::map<std::string, boltprotocol::Value>> parameters;
        };

        struct BearerAuth {
            std::string token;  // The bearer token
        };

        // Variant to hold different authentication types
        using AuthTokenVariant = std::variant<NoAuth, BasicAuth, KerberosAuth, BearerAuth, CustomAuth>;

        // Factory class for creating AuthTokenVariant instances easily
        class AuthTokens {
          public:
            AuthTokens() = delete;  // Static factory methods only

            static AuthTokenVariant none();
            static AuthTokenVariant basic(const std::string& username, const std::string& password, const std::optional<std::string>& realm = std::nullopt);
            static AuthTokenVariant kerberos(const std::string& base64_ticket);
            static AuthTokenVariant bearer(const std::string& token);
            static AuthTokenVariant custom(const std::string& principal, const std::string& credentials, const std::optional<std::string>& realm, const std::string& scheme, const std::optional<std::map<std::string, boltprotocol::Value>>& parameters = std::nullopt);
        };

    }  // namespace config
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_CONFIG_AUTH_TOKEN_H