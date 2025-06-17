#include "neo4j_bolt_transport/config/auth_token.h"

namespace neo4j_bolt_transport {
    namespace config {

        AuthTokenVariant AuthTokens::none() {
            return NoAuth{};
        }

        AuthTokenVariant AuthTokens::basic(const std::string& username, const std::string& password, const std::optional<std::string>& realm) {
            return BasicAuth{username, password, realm};
        }

        AuthTokenVariant AuthTokens::kerberos(const std::string& base64_ticket) {
            return KerberosAuth{base64_ticket};
        }

        AuthTokenVariant AuthTokens::bearer(const std::string& token) {
            return BearerAuth{token};
        }

        AuthTokenVariant AuthTokens::custom(const std::string& principal, const std::string& credentials, const std::optional<std::string>& realm, const std::string& scheme, const std::optional<std::map<std::string, boltprotocol::Value>>& parameters) {
            return CustomAuth{principal, credentials, realm, scheme, parameters};
        }

    }  // namespace config
}  // namespace neo4j_bolt_transport