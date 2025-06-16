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
        // Note: get_optional_int64_from_map was defined in the previous full listing of this file
        // but it's not actually used by HELLO, LOGON, or LOGOFF deserialization.
        // If other functions in this file needed it, it would go here too.

    }  // anonymous namespace

    BoltError deserialize_hello_message_request(PackStreamReader& reader, HelloMessageParams& out_params, const versions::Version& server_negotiated_version) {
        // ... (implementation remains the same, uses helpers from anonymous namespace) ...
        if (reader.has_error()) return reader.get_error();
        out_params = {};

        PackStreamStructure hello_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::HELLO, 1, 1, hello_struct_contents);
        if (err != BoltError::SUCCESS) return err;

        if (hello_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(hello_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        std::shared_ptr<BoltMap> extra_map_sptr;
        try {
            extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(hello_struct_contents.fields[0]));
        } catch (const std::bad_variant_access&) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!extra_map_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        const auto& extra_map_pairs = extra_map_sptr->pairs;
        // using namespace detail_server_request_deserialization; // No longer needed due to anonymous namespace

        auto ua_it = extra_map_pairs.find("user_agent");
        if (ua_it != extra_map_pairs.end() && std::holds_alternative<std::string>(ua_it->second)) {
            try {
                out_params.user_agent = std::get<std::string>(ua_it->second);
            } catch (...) {
                reader.set_error(BoltError::UNKNOWN_ERROR);
                return BoltError::UNKNOWN_ERROR;
            }
        } else {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        bool expect_auth_in_hello = (server_negotiated_version < versions::V5_1);
        if (expect_auth_in_hello) {
            out_params.auth_scheme = get_optional_string_from_map(*extra_map_sptr, "scheme");
            out_params.auth_principal = get_optional_string_from_map(*extra_map_sptr, "principal");
            out_params.auth_credentials = get_optional_string_from_map(*extra_map_sptr, "credentials");
        }
        if (server_negotiated_version.major > 4 || (server_negotiated_version.major == 4 && server_negotiated_version.minor >= 1)) {
            out_params.routing_context = get_optional_map_from_map(*extra_map_sptr, "routing");
        }
        if (server_negotiated_version.major == 4 && (server_negotiated_version.minor == 3 || server_negotiated_version.minor == 4)) {
            out_params.patch_bolt = get_optional_list_string_from_map(*extra_map_sptr, "patch_bolt");
        }
        if (server_negotiated_version.major > 5 || (server_negotiated_version.major == 5 && server_negotiated_version.minor >= 2)) {
            out_params.notifications_min_severity = get_optional_string_from_map(*extra_map_sptr, "notifications_minimum_severity");
            out_params.notifications_disabled_categories = get_optional_list_string_from_map(*extra_map_sptr, "notifications_disabled_categories");
        }
        bool bolt_agent_expected = (server_negotiated_version.major > 5 || (server_negotiated_version.major == 5 && server_negotiated_version.minor >= 3));
        auto bolt_agent_map_val_it = extra_map_pairs.find("bolt_agent");
        if (bolt_agent_map_val_it != extra_map_pairs.end() && std::holds_alternative<std::shared_ptr<BoltMap>>(bolt_agent_map_val_it->second)) {
            auto agent_map_sptr = std::get<std::shared_ptr<BoltMap>>(bolt_agent_map_val_it->second);
            if (agent_map_sptr) {
                HelloMessageParams::BoltAgentInfo agent_info;
                auto product_val = get_optional_string_from_map(*agent_map_sptr, "product");
                if (!product_val.has_value() || product_val.value().empty()) {
                    if (bolt_agent_expected) {
                        reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                        return BoltError::INVALID_MESSAGE_FORMAT;
                    }
                } else {
                    agent_info.product = product_val.value();
                }
                agent_info.platform = get_optional_string_from_map(*agent_map_sptr, "platform");
                agent_info.language = get_optional_string_from_map(*agent_map_sptr, "language");
                agent_info.language_details = get_optional_string_from_map(*agent_map_sptr, "language_details");
                if (!agent_info.product.empty()) {
                    out_params.bolt_agent = agent_info;
                } else if (bolt_agent_expected) {
                    reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                    return BoltError::INVALID_MESSAGE_FORMAT;
                }
            } else if (bolt_agent_expected) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
        } else if (bolt_agent_expected) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        for (const auto& pair : extra_map_pairs) {
            if (pair.first != "user_agent" && pair.first != "scheme" && pair.first != "principal" && pair.first != "credentials" && pair.first != "routing" && pair.first != "patch_bolt" && pair.first != "notifications_minimum_severity" && pair.first != "notifications_disabled_categories" &&
                pair.first != "bolt_agent") {
                try {
                    out_params.other_extra_tokens.emplace(pair.first, pair.second);
                } catch (...) { /* ignore or log out_of_memory */
                }
            }
        }
        return BoltError::SUCCESS;
    }

    BoltError deserialize_logon_message_request(PackStreamReader& reader, LogonMessageParams& out_params) {
        // ... (implementation remains the same) ...
        if (reader.has_error()) return reader.get_error();
        out_params.auth_tokens.clear();

        PackStreamStructure logon_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::LOGON, 1, 1, logon_struct_contents);
        if (err != BoltError::SUCCESS) return err;

        if (logon_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(logon_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        std::shared_ptr<BoltMap> auth_map_sptr;
        try {
            auth_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(logon_struct_contents.fields[0]));
        } catch (const std::bad_variant_access&) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!auth_map_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        try {
            out_params.auth_tokens = std::move(auth_map_sptr->pairs);
        } catch (const std::bad_alloc&) {
            reader.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        return BoltError::SUCCESS;
    }

    BoltError deserialize_logoff_message_request(PackStreamReader& reader) {
        // ... (implementation remains the same) ...
        if (reader.has_error()) return reader.get_error();
        PackStreamStructure logoff_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::LOGOFF, 0, 0, logoff_struct_contents);
        return err;
    }

}  // namespace boltprotocol