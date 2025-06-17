#ifndef NEO4J_BOLT_TRANSPORT_RESULT_SUMMARY_H
#define NEO4J_BOLT_TRANSPORT_RESULT_SUMMARY_H

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"  // For SuccessMessageParams, Value, Version

namespace neo4j_bolt_transport {

    // Forward declarations for Plan, ProfiledPlan, Notification if they become complex classes
    // struct Plan;
    // struct ProfiledPlan;
    // struct Notification;

    struct QueryCounters {
        int64_t nodes_created = 0;
        int64_t nodes_deleted = 0;
        int64_t relationships_created = 0;
        int64_t relationships_deleted = 0;
        int64_t properties_set = 0;
        int64_t labels_added = 0;
        int64_t labels_removed = 0;
        int64_t indexes_added = 0;
        int64_t indexes_removed = 0;
        int64_t constraints_added = 0;
        int64_t constraints_removed = 0;
        bool contains_updates = false;
        bool contains_system_updates = false;
        int64_t system_updates = 0;

        QueryCounters() = default;
    };

    enum class QueryType { UNKNOWN, READ_ONLY, READ_WRITE, WRITE_ONLY, SCHEMA_WRITE };

    // Simplified notification structure
    struct ServerNotification {
        std::string code;
        std::string title;
        std::string description;
        std::optional<std::map<std::string, boltprotocol::Value>> position;  // e.g., {"offset": <int>, "line": <int>, "column": <int>}
        std::string severity;                                                // e.g., "WARNING", "INFORMATION"
        std::string category;                                                // e.g., "HINT", "UNRECOGNIZED" (Bolt 5.2+)

        ServerNotification() = default;
    };

    class ResultSummary {
      public:
        // Constructor takes the raw success message params and connection info
        ResultSummary(boltprotocol::SuccessMessageParams&& server_summary_params,
                      const boltprotocol::versions::Version& bolt_version,
                      bool utc_patch_active,
                      const std::string& server_address,                            // Address of the server that executed the query
                      const std::optional<std::string>& database_name_from_session  // DB name from session config
        );

        const boltprotocol::SuccessMessageParams& raw_params() const {
            return raw_params_;
        }

        QueryType query_type() const {
            return query_type_;
        }
        const QueryCounters& counters() const {
            return counters_;
        }

        const std::string& server_address() const {
            return server_address_;
        }
        const std::string& database_name() const {
            return database_name_;
        }  // Effective DB name for the query

        std::optional<std::chrono::milliseconds> result_available_after() const {
            return result_available_after_ms_;
        }
        std::optional<std::chrono::milliseconds> result_consumed_after() const {
            return result_consumed_after_ms_;
        }

        const std::vector<ServerNotification>& notifications() const {
            return notifications_;
        }

        // std::optional<Plan> plan() const; // TODO if Plan parsing is added
        // std::optional<ProfiledPlan> profiled_plan() const; // TODO if ProfiledPlan parsing is added

      private:
        void parse_metadata(const boltprotocol::versions::Version& bolt_version, bool utc_patch_active);
        void parse_query_type(const boltprotocol::Value& type_val);
        void parse_counters(const boltprotocol::Value& counters_val);
        void parse_notifications(const boltprotocol::Value& notifications_val, const boltprotocol::versions::Version& bolt_version);

        boltprotocol::SuccessMessageParams raw_params_;
        QueryType query_type_ = QueryType::UNKNOWN;
        QueryCounters counters_;
        std::vector<ServerNotification> notifications_;

        std::string server_address_;  // Server that executed the query
        std::string database_name_;   // Effective database for the query

        std::optional<std::chrono::milliseconds> result_available_after_ms_;
        std::optional<std::chrono::milliseconds> result_consumed_after_ms_;
    };

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_RESULT_SUMMARY_H