#ifndef NEO4J_BOLT_TRANSPORT_CONFIG_SESSION_PARAMETERS_H
#define NEO4J_BOLT_TRANSPORT_CONFIG_SESSION_PARAMETERS_H

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"

namespace neo4j_bolt_transport {
    namespace config {

        enum class AccessMode { READ, WRITE };

        struct SessionParameters {
            std::optional<std::string> database_name;
            AccessMode default_access_mode = AccessMode::WRITE;
            std::vector<std::string> initial_bookmarks;
            std::optional<std::string> impersonated_user;

            // Default number of records to fetch in each PULL message.
            // -1 typically means "fetch all remaining".
            // Drivers often have a default like 1000.
            int64_t default_fetch_size = 1000;

            SessionParameters() = default;

            static SessionParameters for_database(const std::string& db_name) {
                SessionParameters p;
                p.database_name = db_name;
                return p;
            }

            SessionParameters& with_database(const std::string& db_name) {
                database_name = db_name;
                return *this;
            }
            SessionParameters& with_default_access_mode(AccessMode mode) {
                default_access_mode = mode;
                return *this;
            }
            SessionParameters& with_bookmarks(const std::vector<std::string>& new_bookmarks) {
                initial_bookmarks = new_bookmarks;
                return *this;
            }
            SessionParameters& with_impersonated_user(const std::string& user) {
                impersonated_user = user;
                return *this;
            }
            SessionParameters& with_fetch_size(int64_t size) {
                default_fetch_size = size;
                return *this;
            }
        };

    }  // namespace config
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_CONFIG_SESSION_PARAMETERS_H