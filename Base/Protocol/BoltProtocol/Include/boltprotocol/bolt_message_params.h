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

    struct HelloMessageParams {
        std::string user_agent;
        std::optional<std::string> auth_scheme;
        std::optional<std::string> auth_principal;
        std::optional<std::string> auth_credentials;
        std::optional<std::map<std::string, Value>> auth_scheme_specific_tokens;  // For complex schemes like custom
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
        std::map<std::string, Value> other_extra_tokens;  // For any other non-standard tokens
    };

    struct RunMessageParams {
        std::string cypher_query;
        std::map<std::string, Value> parameters;
        std::optional<std::vector<std::string>> bookmarks;
        std::optional<int64_t> tx_timeout;                        // Timeout for the implicit transaction
        std::optional<std::map<std::string, Value>> tx_metadata;  // Metadata for the implicit transaction
        std::optional<std::string> mode;                          // "r" for read (Bolt < 5.0)
        std::optional<std::string> db;
        std::optional<std::string> imp_user;
        std::optional<std::string> notifications_min_severity;
        std::optional<std::vector<std::string>> notifications_disabled_categories;
        std::map<std::string, Value> other_extra_fields;  // For any other non-standard fields
    };

    struct DiscardMessageParams {
        std::optional<int64_t> n;    // Number of records to discard (-1 for all)
        std::optional<int64_t> qid;  // Query ID for Bolt 4.0+
    };
    struct PullMessageParams {
        std::optional<int64_t> n;    // Number of records to pull (-1 for all remaining in current batch context)
        std::optional<int64_t> qid;  // Query ID for Bolt 4.0+
    };
    struct BeginMessageParams {
        std::optional<std::vector<std::string>> bookmarks;
        std::optional<int64_t> tx_timeout;                        // Timeout for the explicit transaction
        std::optional<std::map<std::string, Value>> tx_metadata;  // Metadata for the explicit transaction
        std::optional<std::string> mode;                          // "r" for read (Bolt 5.0+)
        std::optional<std::string> db;
        std::optional<std::string> imp_user;
        std::optional<std::string> notifications_min_severity;
        std::optional<std::vector<std::string>> notifications_disabled_categories;
        std::map<std::string, Value> other_extra_fields;  // For any other non-standard fields
    };

    struct CommitMessageParams { /* PSS field is an empty map {} */
    };
    struct RollbackMessageParams { /* PSS field is an empty map {} */
    };

    struct RouteMessageParams {
        std::map<std::string, Value> routing_table_context;
        std::vector<std::string> bookmarks;
        std::optional<std::string> db_name_for_v43;  // Bolt 4.3: db (String or null)
        // Bolt 4.4+: extra map (can contain "db" and/or "imp_user")
        // Bolt 5.1+: extra map can also contain "notifications_min_severity", "notifications_disabled_categories"
        std::optional<std::map<std::string, Value>> extra_for_v44_plus;
    };

    struct TelemetryMessageParams {
        std::map<std::string, Value> metadata;  // api (int)
    };
    struct LogonMessageParams {
        std::map<std::string, Value> auth_tokens;
    };
    struct LogoffMessageParams { /* No fields */
    };

    struct SuccessMessageParams {
        std::map<std::string, Value> metadata;
    };
    struct RecordMessageParams {
        std::vector<Value> fields;
    };
    struct FailureMessageParams {
        std::map<std::string, Value> metadata;  // code (String), message (String)
    };

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_MESSAGE_PARAMS_H