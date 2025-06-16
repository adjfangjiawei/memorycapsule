#ifndef BOLTPROTOCOL_MESSAGE_PARAMS_H
#define BOLTPROTOCOL_MESSAGE_PARAMS_H

#include <map>
#include <memory>    // For std::shared_ptr
#include <optional>  // For std::optional
#include <string>
#include <vector>

#include "bolt_core_types.h"  // For Value, BoltMap, BoltList

// Ensure versions are available if any params struct needs them (currently not directly)
// #include "bolt_errors_versions.h"

namespace boltprotocol {

    // --- Message Parameter Structures ---

    struct HelloMessageParams {
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
        std::map<std::string, Value> parameters;
        std::map<std::string, Value> extra_metadata;
    };

    struct DiscardMessageParams {
        std::optional<int64_t> n;
        std::optional<int64_t> qid;
    };

    struct PullMessageParams {
        std::optional<int64_t> n;
        std::optional<int64_t> qid;
    };

    struct BeginMessageParams {
        std::map<std::string, Value> extra;
    };

    struct CommitMessageParams { /* PSS field is an empty map {} */
    };
    struct RollbackMessageParams { /* PSS field is an empty map {} */
    };

    struct RouteMessageParams {
        std::map<std::string, Value> routing_context;
        std::vector<std::string> bookmarks;
        std::optional<std::string> db_name;
        std::optional<std::string> impersonated_user;
    };

    struct TelemetryMessageParams {
        std::map<std::string, Value> metadata;
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
        std::map<std::string, Value> metadata;
    };

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_MESSAGE_PARAMS_H