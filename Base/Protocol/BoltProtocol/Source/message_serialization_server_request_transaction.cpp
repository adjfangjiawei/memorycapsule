#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"  // For deserialize_message_structure_prelude
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    namespace {
        std::optional<std::string> get_optional_string_from_map(const BoltMap& map, const std::string& key) {
            auto it = map.pairs.find(key);
            if (it != map.pairs.end() && std::holds_alternative<std::string>(it->second)) {
                try {
                    return std::get<std::string>(it->second);
                } catch (...) {
                }
            }
            return std::nullopt;
        }
        std::optional<std::vector<std::string>> get_optional_list_string_from_map(const BoltMap& map, const std::string& key) {
            auto it = map.pairs.find(key);
            if (it != map.pairs.end() && std::holds_alternative<std::shared_ptr<BoltList>>(it->second)) {
                auto list_sptr = std::get<std::shared_ptr<BoltList>>(it->second);
                if (list_sptr) {
                    std::vector<std::string> result;
                    result.reserve(list_sptr->elements.size());
                    bool all_strings = true;
                    for (const auto& element : list_sptr->elements) {
                        if (std::holds_alternative<std::string>(element)) {
                            try {
                                result.push_back(std::get<std::string>(element));
                            } catch (...) {
                                all_strings = false;
                                break;
                            }
                        } else {
                            all_strings = false;
                            break;
                        }
                    }
                    if (all_strings) return result;
                }
            }
            return std::nullopt;
        }
        std::optional<std::map<std::string, Value>> get_optional_map_from_map(const BoltMap& map, const std::string& key) {
            auto it = map.pairs.find(key);
            if (it != map.pairs.end() && std::holds_alternative<std::shared_ptr<BoltMap>>(it->second)) {
                auto inner_map_sptr = std::get<std::shared_ptr<BoltMap>>(it->second);
                if (inner_map_sptr) {
                    try {
                        return inner_map_sptr->pairs;
                    } catch (...) {
                    }
                }
            }
            return std::nullopt;
        }
        std::optional<int64_t> get_optional_int64_from_map(const BoltMap& map, const std::string& key) {
            auto it = map.pairs.find(key);
            if (it != map.pairs.end() && std::holds_alternative<int64_t>(it->second)) {
                try {
                    return std::get<int64_t>(it->second);
                } catch (...) {
                }
            }
            return std::nullopt;
        }
    }  // namespace

    BoltError deserialize_begin_message_request(PackStreamReader& reader, BeginMessageParams& out_params, const versions::Version& server_negotiated_version) {
        if (reader.has_error()) return reader.get_error();
        out_params = {};

        PackStreamStructure begin_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::BEGIN, 1, 1, begin_struct_contents);
        if (err != BoltError::SUCCESS) return err;

        if (begin_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(begin_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(begin_struct_contents.fields[0]));
        if (!extra_map_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        const auto& extra_map_pairs_ref = extra_map_sptr->pairs;
        if (server_negotiated_version.major >= 3) {
            out_params.bookmarks = get_optional_list_string_from_map(*extra_map_sptr, "bookmarks");
            out_params.tx_timeout = get_optional_int64_from_map(*extra_map_sptr, "tx_timeout");
            out_params.tx_metadata = get_optional_map_from_map(*extra_map_sptr, "tx_metadata");
            out_params.mode = get_optional_string_from_map(*extra_map_sptr, "mode");
        }
        if (server_negotiated_version.major >= 4) {
            out_params.db = get_optional_string_from_map(*extra_map_sptr, "db");
            out_params.imp_user = get_optional_string_from_map(*extra_map_sptr, "imp_user");
        }
        if (server_negotiated_version.major > 5 || (server_negotiated_version.major == 5 && server_negotiated_version.minor >= 2)) {
            out_params.notifications_min_severity = get_optional_string_from_map(*extra_map_sptr, "notifications_minimum_severity");
            out_params.notifications_disabled_categories = get_optional_list_string_from_map(*extra_map_sptr, "notifications_disabled_categories");
        }
        for (const auto& pair : extra_map_pairs_ref) {
            bool is_typed_field = ((server_negotiated_version.major >= 3 && (pair.first == "bookmarks" || pair.first == "tx_timeout" || pair.first == "tx_metadata" || pair.first == "mode")) || (server_negotiated_version.major >= 4 && (pair.first == "db" || pair.first == "imp_user")) ||
                                   ((server_negotiated_version.major > 5 || (server_negotiated_version.major == 5 && server_negotiated_version.minor >= 2)) && (pair.first == "notifications_minimum_severity" || pair.first == "notifications_disabled_categories")));
            if (!is_typed_field) {
                try {
                    out_params.other_extra_fields.emplace(pair.first, pair.second);
                } catch (...) { /* ignore or log */
                }
            }
        }
        return BoltError::SUCCESS;
    }

    BoltError deserialize_commit_message_request(PackStreamReader& reader) {
        if (reader.has_error()) return reader.get_error();
        // CommitMessageParams is empty struct, no out_params needed.

        PackStreamStructure commit_struct_contents;
        // COMMIT PSS (Bolt 3+) has 1 field: an empty map {}.
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::COMMIT, 1, 1, commit_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }

        // Validate the field is indeed a map (preferably empty).
        if (commit_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(commit_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        auto map_sptr = std::get<std::shared_ptr<BoltMap>>(commit_struct_contents.fields[0]);
        if (!map_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);  // Map field was a null shared_ptr
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        // Specification: "Fields: No fields." but the PackStream structure for COMMIT is `COMMIT {}`
        // This means the PSS has one field, which is an empty map.
        // We can optionally check if map_sptr->pairs is empty for stricter validation.
        // if (!map_sptr->pairs.empty()) {
        //     reader.set_error(BoltError::INVALID_MESSAGE_FORMAT); // Expected empty map
        //     return BoltError::INVALID_MESSAGE_FORMAT;
        // }
        return BoltError::SUCCESS;
    }

    BoltError deserialize_rollback_message_request(PackStreamReader& reader) {
        if (reader.has_error()) return reader.get_error();
        // RollbackMessageParams is empty.

        PackStreamStructure rollback_struct_contents;
        // ROLLBACK PSS (Bolt 3+) has 1 field: an empty map {}.
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::ROLLBACK, 1, 1, rollback_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }
        // Validate the field.
        if (rollback_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(rollback_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        auto map_sptr = std::get<std::shared_ptr<BoltMap>>(rollback_struct_contents.fields[0]);
        if (!map_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        // The map should be empty.
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol