#ifndef BOLTPROTOCOL_MESSAGE_PARAMS_H
#define BOLTPROTOCOL_MESSAGE_PARAMS_H

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bolt_core_types.h"
#include "bolt_errors_versions.h"

namespace boltprotocol {

    // --- Message Parameter Structures ---

    struct HelloMessageParams { /* ... (remains as in previous version) ... */
        std::string user_agent;
        std::optional<std::string> auth_scheme;
        std::optional<std::string> auth_principal;
        std::optional<std::string> auth_credentials;
        std::optional<std::map<std::string, Value>> auth_scheme_specific_tokens;
        std::optional<std::map<std::string, Value>> routing_context;
        std::optional<std::vector<std::string>> patch_bolt;
        std::optional<std::string> notifications_min_severity;
        std::optional<std::vector<std::string>> notifications_disabled_categories;
        struct BoltAgentInfo {
            std::string product;
            std::optional<std::string> platform;
            std::optional<std::string> language;
            std::optional<std::string> language_details;
        };
        std::optional<BoltAgentInfo> bolt_agent;
        std::map<std::string, Value> other_extra_tokens;
    };

    struct RunMessageParams { /* ... (remains as in previous version) ... */
        std::string cypher_query;
        std::map<std::string, Value> parameters;
        std::optional<std::vector<std::string>> bookmarks;
        std::optional<int64_t> tx_timeout;
        std::optional<std::map<std::string, Value>> tx_metadata;
        std::optional<std::string> mode;
        std::optional<std::string> db;
        std::optional<std::string> imp_user;
        std::optional<std::string> notifications_min_severity;
        std::optional<std::vector<std::string>> notifications_disabled_categories;
        std::map<std::string, Value> other_extra_fields;
    };

    struct DiscardMessageParams { /* ... (remains as in previous version) ... */
        std::optional<int64_t> n;
        std::optional<int64_t> qid;
    };
    struct PullMessageParams { /* ... (remains as in previous version) ... */
        std::optional<int64_t> n;
        std::optional<int64_t> qid;
    };
    struct BeginMessageParams { /* ... (remains as in previous version) ... */
        std::optional<std::vector<std::string>> bookmarks;
        std::optional<int64_t> tx_timeout;
        std::optional<std::map<std::string, Value>> tx_metadata;
        std::optional<std::string> mode;
        std::optional<std::string> db;
        std::optional<std::string> imp_user;
        std::optional<std::string> notifications_min_severity;
        std::optional<std::vector<std::string>> notifications_disabled_categories;
        std::map<std::string, Value> other_extra_fields;
    };

    struct CommitMessageParams { /* PSS field is an empty map {} */
    };
    struct RollbackMessageParams { /* PSS field is an empty map {} */
    };

    struct RouteMessageParams {
        // Field 1: routing::Dictionary
        std::map<std::string, Value> routing_table_context;  // Renamed for clarity from routing_context to avoid clash with HELLO

        // Field 2: bookmarks::List<String>
        std::vector<std::string> bookmarks;

        // For Bolt 4.3, Field 3 is db::String (or null)
        std::optional<std::string> db_name_for_v43;

        // For Bolt 4.4+, Field 3 is extra::Dictionary(db::String, imp_user::String)
        // This map can contain "db" and/or "imp_user".
        std::optional<std::map<std::string, Value>> extra_for_v44_plus;
    };

    struct TelemetryMessageParams { /* ... (remains as in previous version) ... */
        std::map<std::string, Value> metadata;
    };
    struct LogonMessageParams { /* ... (remains as in previous version) ... */
        std::map<std::string, Value> auth_tokens;
    };
    struct LogoffMessageParams { /* No fields */
    };

    struct SuccessMessageParams { /* ... (remains as in previous version) ... */
        std::map<std::string, Value> metadata;
    };
    struct RecordMessageParams { /* ... (remains as in previous version) ... */
        std::vector<Value> fields;
    };
    struct FailureMessageParams { /* ... (remains as in previous version) ... */
        std::map<std::string, Value> metadata;
    };

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_MESSAGE_PARAMS_H