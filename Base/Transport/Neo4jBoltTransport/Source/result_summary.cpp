#include "neo4j_bolt_transport/result_summary.h"

#include <iostream>  // For potential debug

namespace neo4j_bolt_transport {

    // Helper to safely get a string from a Bolt Value
    std::optional<std::string> get_string_val(const boltprotocol::Value& val) {
        if (std::holds_alternative<std::string>(val)) {
            return std::get<std::string>(val);
        }
        return std::nullopt;
    }

    // Helper to safely get an int64 from a Bolt Value
    std::optional<int64_t> get_int64_val(const boltprotocol::Value& val) {
        if (std::holds_alternative<int64_t>(val)) {
            return std::get<int64_t>(val);
        }
        return std::nullopt;
    }

    // Helper to safely get a bool from a Bolt Value
    std::optional<bool> get_bool_val(const boltprotocol::Value& val) {
        if (std::holds_alternative<bool>(val)) {
            return std::get<bool>(val);
        }
        return std::nullopt;
    }

    ResultSummary::ResultSummary(boltprotocol::SuccessMessageParams&& server_summary_params, const boltprotocol::versions::Version& bolt_version, bool utc_patch_active, const std::string& srv_address, const std::optional<std::string>& db_name_from_session)
        : raw_params_(std::move(server_summary_params)), server_address_(srv_address) {
        // Determine effective database name
        auto db_it = raw_params_.metadata.find("db");
        if (db_it != raw_params_.metadata.end()) {
            if (auto db_str = get_string_val(db_it->second)) {
                database_name_ = *db_str;
            }
        }
        if (database_name_.empty() && db_name_from_session.has_value()) {
            database_name_ = *db_name_from_session;
        }
        if (database_name_.empty()) {
            // Fallback if not in summary and not in session (e.g. system db for older Neo4j)
            database_name_ = "system";  // Or "" if that's preferred for default
        }

        parse_metadata(bolt_version, utc_patch_active);
    }

    void ResultSummary::parse_query_type(const boltprotocol::Value& type_val_variant) {
        if (auto type_str_opt = get_string_val(type_val_variant)) {
            const std::string& type_str = *type_str_opt;
            if (type_str == "r")
                query_type_ = QueryType::READ_ONLY;
            else if (type_str == "rw")
                query_type_ = QueryType::READ_WRITE;
            else if (type_str == "w")
                query_type_ = QueryType::WRITE_ONLY;
            else if (type_str == "s")
                query_type_ = QueryType::SCHEMA_WRITE;
            else
                query_type_ = QueryType::UNKNOWN;
        }
    }

    void ResultSummary::parse_counters(const boltprotocol::Value& counters_val_variant) {
        if (std::holds_alternative<std::shared_ptr<boltprotocol::BoltMap>>(counters_val_variant)) {
            const auto& counters_map_ptr = std::get<std::shared_ptr<boltprotocol::BoltMap>>(counters_val_variant);
            if (counters_map_ptr) {
                const auto& m = counters_map_ptr->pairs;
                auto get_counter = [&](const std::string& key) {
                    auto it = m.find(key);
                    if (it != m.end()) {
                        return get_int64_val(it->second).value_or(0);
                    }
                    return int64_t{0};
                };
                auto get_bool_counter = [&](const std::string& key) {
                    auto it = m.find(key);
                    if (it != m.end()) {
                        return get_bool_val(it->second).value_or(false);
                    }
                    return false;
                };

                counters_.nodes_created = get_counter("nodes-created");
                counters_.nodes_deleted = get_counter("nodes-deleted");
                counters_.relationships_created = get_counter("relationships-created");
                counters_.relationships_deleted = get_counter("relationships-deleted");
                counters_.properties_set = get_counter("properties-set");
                counters_.labels_added = get_counter("labels-added");
                counters_.labels_removed = get_counter("labels-removed");
                counters_.indexes_added = get_counter("indexes-added");
                counters_.indexes_removed = get_counter("indexes-removed");
                counters_.constraints_added = get_counter("constraints-added");
                counters_.constraints_removed = get_counter("constraints-removed");
                counters_.system_updates = get_counter("system-updates");                         // Bolt 4.3+
                counters_.contains_system_updates = get_bool_counter("contains-system-updates");  // Bolt 5.0+

                // contains-updates logic:
                // True if any of the specific counters > 0 OR if "contains-updates" is explicitly true
                counters_.contains_updates = (counters_.nodes_created > 0 || counters_.nodes_deleted > 0 || counters_.relationships_created > 0 || counters_.relationships_deleted > 0 || counters_.properties_set > 0 || counters_.labels_added > 0 || counters_.labels_removed > 0 ||
                                              counters_.indexes_added > 0 || counters_.indexes_removed > 0 || counters_.constraints_added > 0 || counters_.constraints_removed > 0);
                // If server provides "contains-updates", respect it
                auto it_contains_updates = m.find("contains-updates");
                if (it_contains_updates != m.end()) {
                    if (auto b_val = get_bool_val(it_contains_updates->second)) {
                        counters_.contains_updates = *b_val;
                    }
                }

                if (counters_.system_updates > 0 && !counters_.contains_system_updates) {
                    // if system_updates > 0, contains_system_updates should be true.
                    // This might indicate an older server version or an inconsistency.
                    // For safety, set contains_system_updates if system_updates is positive.
                    counters_.contains_system_updates = true;
                }
            }
        }
    }

    void ResultSummary::parse_notifications(const boltprotocol::Value& notifications_val_variant, const boltprotocol::versions::Version& bolt_version) {
        if (std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(notifications_val_variant)) {
            const auto& list_ptr = std::get<std::shared_ptr<boltprotocol::BoltList>>(notifications_val_variant);
            if (list_ptr) {
                notifications_.reserve(list_ptr->elements.size());
                for (const auto& item_val : list_ptr->elements) {
                    if (std::holds_alternative<std::shared_ptr<boltprotocol::BoltMap>>(item_val)) {
                        const auto& map_ptr = std::get<std::shared_ptr<boltprotocol::BoltMap>>(item_val);
                        if (map_ptr) {
                            ServerNotification notif;
                            const auto& m = map_ptr->pairs;
                            auto find_str = [&](const std::string& key) -> std::optional<std::string> {
                                auto it = m.find(key);
                                if (it != m.end()) return get_string_val(it->second);
                                return std::nullopt;
                            };

                            notif.code = find_str("code").value_or("");
                            notif.title = find_str("title").value_or("");
                            notif.description = find_str("description").value_or("");
                            notif.severity = find_str("severity").value_or("");  // Bolt 4.1+
                            if (bolt_version.major > 5 || (bolt_version.major == 5 && bolt_version.minor >= 2)) {
                                notif.category = find_str("category").value_or("");
                            }

                            auto pos_it = m.find("position");
                            if (pos_it != m.end() && std::holds_alternative<std::shared_ptr<boltprotocol::BoltMap>>(pos_it->second)) {
                                const auto& pos_map_ptr = std::get<std::shared_ptr<boltprotocol::BoltMap>>(pos_it->second);
                                if (pos_map_ptr) notif.position = pos_map_ptr->pairs;
                            }
                            notifications_.push_back(std::move(notif));
                        }
                    }
                }
            }
        }
    }

    void ResultSummary::parse_metadata(const boltprotocol::versions::Version& bolt_version, bool /*utc_patch_active*/) {
        // Extract common fields
        auto t_start_it = raw_params_.metadata.find("t_first");  // Time to first record (RUN response) or available (PULL/DISCARD response)
        if (t_start_it != raw_params_.metadata.end()) {
            if (auto ms = get_int64_val(t_start_it->second)) {
                result_available_after_ms_ = std::chrono::milliseconds(*ms);
            }
        }

        auto t_end_it = raw_params_.metadata.find("t_last");  // Time to last record (PULL/DISCARD response)
        if (t_end_it != raw_params_.metadata.end()) {
            if (auto ms = get_int64_val(t_end_it->second)) {
                result_consumed_after_ms_ = std::chrono::milliseconds(*ms);
            }
        }

        auto type_it = raw_params_.metadata.find("type");
        if (type_it != raw_params_.metadata.end()) {
            parse_query_type(type_it->second);
        }

        auto counters_it = raw_params_.metadata.find("stats");
        if (counters_it != raw_params_.metadata.end()) {
            parse_counters(counters_it->second);
        }

        auto notifications_it = raw_params_.metadata.find("notifications");
        if (notifications_it != raw_params_.metadata.end()) {
            parse_notifications(notifications_it->second, bolt_version);
        }

        // Plan and Profile parsing would go here if implemented
        // auto plan_it = raw_params_.metadata.find("plan");
        // if (plan_it != raw_params_.metadata.end()) { ... parse plan ... }
        // auto profile_it = raw_params_.metadata.find("profile"); // or "profiled-plan"
        // if (profile_it != raw_params_.metadata.end()) { ... parse profile ... }
    }

}  // namespace neo4j_bolt_transport