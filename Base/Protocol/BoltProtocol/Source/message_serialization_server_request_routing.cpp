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
    }  // namespace

    BoltError deserialize_route_message_request(PackStreamReader& reader, RouteMessageParams& out_params, const versions::Version& server_negotiated_version) {
        if (reader.has_error()) return reader.get_error();
        out_params = {};  // Clear params

        // ROUTE message introduced in 4.3. PSS has 3 fields.
        if (server_negotiated_version < versions::Version(4, 3)) {
            reader.set_error(BoltError::UNSUPPORTED_PROTOCOL_VERSION);  // Or INVALID_MESSAGE_FORMAT
            return reader.get_error();
        }

        PackStreamStructure route_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::ROUTE, 3, 3, route_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }

        if (route_struct_contents.fields.size() != 3) {  // Defensive, prelude should catch this
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        // Field 0: routing_context (Map)
        if (!std::holds_alternative<std::shared_ptr<BoltMap>>(route_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        auto route_context_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(route_struct_contents.fields[0]));
        if (!route_context_map_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        try {
            out_params.routing_table_context = std::move(route_context_map_sptr->pairs);
        } catch (...) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        // Field 1: bookmarks (List<String>)
        if (!std::holds_alternative<std::shared_ptr<BoltList>>(route_struct_contents.fields[1])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        auto bookmarks_list_sptr = std::get<std::shared_ptr<BoltList>>(std::move(route_struct_contents.fields[1]));
        if (!bookmarks_list_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        out_params.bookmarks.clear();
        try {
            out_params.bookmarks.reserve(bookmarks_list_sptr->elements.size());
            for (const auto& bm_val : bookmarks_list_sptr->elements) {
                if (std::holds_alternative<std::string>(bm_val)) {
                    out_params.bookmarks.push_back(std::get<std::string>(bm_val));
                } else {
                    reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                    return BoltError::INVALID_MESSAGE_FORMAT;  // Bookmark not a string
                }
            }
        } catch (...) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        // Field 2: Varies by version (db string for 4.3, extra map for 4.4+)
        if (server_negotiated_version.major == 4 && server_negotiated_version.minor == 3) {  // Bolt 4.3
            if (std::holds_alternative<std::string>(route_struct_contents.fields[2])) {
                out_params.db_name_for_v43 = std::get<std::string>(std::move(route_struct_contents.fields[2]));
            } else if (std::holds_alternative<std::nullptr_t>(route_struct_contents.fields[2])) {
                out_params.db_name_for_v43 = std::nullopt;
            } else {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;  // Expected string or null
            }
        } else if (server_negotiated_version.major > 4 || (server_negotiated_version.major == 4 && server_negotiated_version.minor >= 4)) {  // Bolt 4.4+
            if (!std::holds_alternative<std::shared_ptr<BoltMap>>(route_struct_contents.fields[2])) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
            auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(route_struct_contents.fields[2]));
            if (extra_map_sptr) {  // Map can be empty, but shared_ptr should be non-null
                out_params.extra_for_v44_plus = std::move(extra_map_sptr->pairs);
            } else {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;  // Extra map field was null
            }
        }
        // For Bolt 5.0+, the PSS structure is like 4.4 (3 fields, 3rd is extra map).
        // The semantic meaning of routing_table_context and extra_for_v44_plus for ROUTE V2 is handled
        // by how the client populates RouteMessageParams and how server interprets them.

        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol