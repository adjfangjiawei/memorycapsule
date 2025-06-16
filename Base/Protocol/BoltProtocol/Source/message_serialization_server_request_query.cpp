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

    namespace {  // Anonymous namespace for internal linkage helper functions

        std::optional<std::string> get_optional_string_from_map(const BoltMap& map, const std::string& key) {
            auto it = map.pairs.find(key);
            if (it != map.pairs.end() && std::holds_alternative<std::string>(it->second)) {
                return std::get<std::string>(it->second);
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
                            result.push_back(std::get<std::string>(element));
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
                    return inner_map_sptr->pairs;
                }
            }
            return std::nullopt;
        }

        std::optional<int64_t> get_optional_int64_from_map(const BoltMap& map, const std::string& key) {
            auto it = map.pairs.find(key);
            if (it != map.pairs.end() && std::holds_alternative<int64_t>(it->second)) {
                return std::get<int64_t>(it->second);
            }
            return std::nullopt;
        }

    }  // anonymous namespace

    BoltError deserialize_run_message_request(PackStreamReader& reader, RunMessageParams& out_params, const versions::Version& server_negotiated_version) {
        // ... (implementation remains the same, uses helpers from anonymous namespace) ...
        if (reader.has_error()) return reader.get_error();
        out_params = {};

        PackStreamStructure run_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::RUN, 3, 3, run_struct_contents);
        if (err != BoltError::SUCCESS) return err;

        if (!std::holds_alternative<std::string>(run_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        try {
            out_params.cypher_query = std::get<std::string>(std::move(run_struct_contents.fields[0]));
        } catch (const std::bad_variant_access&) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::bad_alloc&) {
            reader.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
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
        } catch (const std::bad_alloc&) {
            reader.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
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

        const auto& extra_map_pairs = extra_map_sptr->pairs;
        // using namespace detail_server_request_deserialization; // No longer needed

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

        for (const auto& pair : extra_map_pairs) {
            if (pair.first != "bookmarks" && pair.first != "tx_timeout" && pair.first != "tx_metadata" && pair.first != "mode" && pair.first != "db" && pair.first != "imp_user" && pair.first != "notifications_minimum_severity" && pair.first != "notifications_disabled_categories") {
                try {
                    out_params.other_extra_fields.emplace(pair.first, pair.second);
                } catch (...) { /* ignore or log */
                }
            }
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol