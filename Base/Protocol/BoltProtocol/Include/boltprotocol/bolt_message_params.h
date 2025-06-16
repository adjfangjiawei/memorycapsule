#ifndef BOLTPROTOCOL_MESSAGE_PARAMS_H
#define BOLTPROTOCOL_MESSAGE_PARAMS_H

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bolt_core_types.h"
#include "bolt_errors_versions.h"  // For versions::Version if needed by params (e.g. for version checks)

namespace boltprotocol {

    // --- Message Parameter Structures ---

    struct HelloMessageParams {  // ... (remains as in previous version) ...
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

    struct RunMessageParams {
        std::string cypher_query;
        std::map<std::string, Value> parameters;  // Cypher parameters

        // Specific 'extra' fields for RUN, typed for clarity and safety
        std::optional<std::vector<std::string>> bookmarks;                          // Bolt 3+
        std::optional<int64_t> tx_timeout;                                          // Bolt 3+ (milliseconds)
        std::optional<std::map<std::string, Value>> tx_metadata;                    // Bolt 3+
        std::optional<std::string> mode;                                            // Bolt 3+ ("r" or "w")
        std::optional<std::string> db;                                              // Bolt 4.0+
        std::optional<std::string> imp_user;                                        // Bolt 4.4+ (impersonated user for auto-commit RUN)
        std::optional<std::string> notifications_min_severity;                      // Bolt 5.2+
        std::optional<std::vector<std::string>> notifications_disabled_categories;  // Bolt 5.2+

        // For any other custom or future fields in the 'extra' dictionary
        std::map<std::string, Value> other_extra_fields;
    };

    struct DiscardMessageParams {  // ... (remains as in previous version) ...
        std::optional<int64_t> n;
        std::optional<int64_t> qid;
    };

    struct PullMessageParams {  // ... (remains as in previous version) ...
        std::optional<int64_t> n;
        std::optional<int64_t> qid;
    };

    struct BeginMessageParams {
        // Specific 'extra' fields for BEGIN, typed
        std::optional<std::vector<std::string>> bookmarks;                          // Bolt 3+
        std::optional<int64_t> tx_timeout;                                          // Bolt 3+ (milliseconds)
        std::optional<std::map<std::string, Value>> tx_metadata;                    // Bolt 3+
        std::optional<std::string> mode;                                            // Bolt 3+ ("r" or "w", defaults to "w")
        std::optional<std::string> db;                                              // Bolt 4.0+
        std::optional<std::string> imp_user;                                        // Bolt 4.0+ (impersonated user for explicit BEGIN)
        std::optional<std::string> notifications_min_severity;                      // Bolt 5.2+
        std::optional<std::vector<std::string>> notifications_disabled_categories;  // Bolt 5.2+

        // For any other custom or future fields in the 'extra' dictionary
        std::map<std::string, Value> other_extra_fields;
    };

    struct CommitMessageParams { /* PSS field is an empty map {} */
    };
    struct RollbackMessageParams { /* PSS field is an empty map {} */
    };

    struct RouteMessageParams {  // ... (remains as in previous version) ...
        std::map<std::string, Value> routing_context;
        std::vector<std::string> bookmarks;
        std::optional<std::string> db_name;
        std::optional<std::string> impersonated_user;
    };

    struct TelemetryMessageParams {  // ... (remains as in previous version) ...
        std::map<std::string, Value> metadata;
    };

    struct LogonMessageParams {  // ... (remains as in previous version) ...
        std::map<std::string, Value> auth_tokens;
    };

    struct LogoffMessageParams { /* No fields */
    };

    struct SuccessMessageParams {  // ... (remains as in previous version) ...
        std::map<std::string, Value> metadata;
    };
    struct RecordMessageParams {  // ... (remains as in previous version) ...
        std::vector<Value> fields;
    };
    struct FailureMessageParams {  // ... (remains as in previous version) ...
        std::map<std::string, Value> metadata;
    };

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_MESSAGE_PARAMS_H