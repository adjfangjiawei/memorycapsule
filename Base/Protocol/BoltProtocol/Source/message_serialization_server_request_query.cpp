#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
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

    BoltError deserialize_run_message_request(PackStreamReader& reader, RunMessageParams& out_params, const versions::Version& server_negotiated_version) {
        if (reader.has_error()) return reader.get_error();
        out_params = {};

        PackStreamStructure run_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::RUN, 3, 3, run_struct_contents);
        if (err != BoltError::SUCCESS) return err;

        if (run_struct_contents.fields.size() < 3) {  // Defensive, prelude should catch this
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        if (!std::holds_alternative<std::string>(run_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        try {
            out_params.cypher_query = std::get<std::string>(std::move(run_struct_contents.fields[0]));
        } catch (...) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct_contents.fields[1])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        auto params_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_contents.fields[1]));
        if (!params_map_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        try {
            out_params.parameters = std::move(params_map_sptr->pairs);
        } catch (...) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct_contents.fields[2])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_contents.fields[2]));
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
        }
        if (server_negotiated_version.major > 4 || (server_negotiated_version.major == 4 && server_negotiated_version.minor >= 4)) {
            out_params.imp_user = get_optional_string_from_map(*extra_map_sptr, "imp_user");
        }
        if (server_negotiated_version.major > 5 || (server_negotiated_version.major == 5 && server_negotiated_version.minor >= 2)) {
            out_params.notifications_min_severity = get_optional_string_from_map(*extra_map_sptr, "notifications_minimum_severity");
            out_params.notifications_disabled_categories = get_optional_list_string_from_map(*extra_map_sptr, "notifications_disabled_categories");
        }
        for (const auto& pair : extra_map_pairs_ref) {
            bool is_typed_field = ((server_negotiated_version.major >= 3 && (pair.first == "bookmarks" || pair.first == "tx_timeout" || pair.first == "tx_metadata" || pair.first == "mode")) || (server_negotiated_version.major >= 4 && pair.first == "db") ||
                                   ((server_negotiated_version.major > 4 || (server_negotiated_version.major == 4 && server_negotiated_version.minor >= 4)) && pair.first == "imp_user") ||
                                   ((server_negotiated_version.major > 5 || (server_negotiated_version.major == 5 && server_negotiated_version.minor >= 2)) && (pair.first == "notifications_minimum_severity" || pair.first == "notifications_disabled_categories")));
            if (!is_typed_field) {
                try {
                    out_params.other_extra_fields.emplace(pair.first, pair.second);
                } catch (...) { /* ignore or log out_of_memory for map */
                }
            }
        }
        return BoltError::SUCCESS;
    }

    BoltError deserialize_pull_message_request(PackStreamReader& reader, PullMessageParams& out_params, const versions::Version& server_negotiated_version) {
        if (reader.has_error()) return reader.get_error();
        out_params.n = std::nullopt;
        out_params.qid = std::nullopt;

        PackStreamStructure pull_struct_contents;
        BoltError err;

        if (server_negotiated_version.major >= 4) {  // Bolt 4.0+ PULL has 1 field (extra map)
            err = deserialize_message_structure_prelude(reader, MessageTag::PULL, 1, 1, pull_struct_contents);
            if (err != BoltError::SUCCESS) return err;

            if (pull_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(pull_struct_contents.fields[0])) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
            auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(pull_struct_contents.fields[0]));
            if (!extra_map_sptr) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
            out_params.n = get_optional_int64_from_map(*extra_map_sptr, "n");
            out_params.qid = get_optional_int64_from_map(*extra_map_sptr, "qid");
            if (!out_params.n.has_value()) {  // 'n' is mandatory in Bolt 4.0+ PULL
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                // return BoltError::INVALID_MESSAGE_FORMAT; // Be strict or allow for now?
            }
        } else {  // Bolt < 4.0 (PULL_ALL) has 0 fields
            err = deserialize_message_structure_prelude(reader, MessageTag::PULL, 0, 0, pull_struct_contents);
            if (err != BoltError::SUCCESS) return err;
            // For PULL_ALL, n is implicitly -1 (all), qid is implicitly -1 (last query)
            out_params.n = -1;
            out_params.qid = -1;
        }
        return BoltError::SUCCESS;
    }

    BoltError deserialize_discard_message_request(PackStreamReader& reader, DiscardMessageParams& out_params, const versions::Version& server_negotiated_version) {
        if (reader.has_error()) return reader.get_error();
        out_params.n = std::nullopt;
        out_params.qid = std::nullopt;

        PackStreamStructure discard_struct_contents;
        BoltError err;

        if (server_negotiated_version.major >= 4) {  // Bolt 4.0+ DISCARD has 1 field (extra map)
            err = deserialize_message_structure_prelude(reader, MessageTag::DISCARD, 1, 1, discard_struct_contents);
            if (err != BoltError::SUCCESS) return err;

            if (discard_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(discard_struct_contents.fields[0])) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
            auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(discard_struct_contents.fields[0]));
            if (!extra_map_sptr) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
            out_params.n = get_optional_int64_from_map(*extra_map_sptr, "n");
            out_params.qid = get_optional_int64_from_map(*extra_map_sptr, "qid");
            if (!out_params.n.has_value()) {  // 'n' is mandatory in Bolt 4.0+ DISCARD
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                // return BoltError::INVALID_MESSAGE_FORMAT;
            }
        } else {  // Bolt < 4.0 (DISCARD_ALL) has 0 fields
            err = deserialize_message_structure_prelude(reader, MessageTag::DISCARD, 0, 0, discard_struct_contents);
            if (err != BoltError::SUCCESS) return err;
            out_params.n = -1;
            out_params.qid = -1;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol