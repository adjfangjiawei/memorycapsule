#include <exception>
#include <map>
#include <memory>
#include <optional>  // For std::optional in HelloMessageParams
#include <string>
#include <variant>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    // Helper to safely get a string value from a BoltMap Value if key exists
    std::optional<std::string> get_optional_string_from_map(const BoltMap& map, const std::string& key) {
        auto it = map.pairs.find(key);
        if (it != map.pairs.end() && std::holds_alternative<std::string>(it->second)) {
            return std::get<std::string>(it->second);
        }
        return std::nullopt;
    }

    // Helper to safely get a list of strings from a BoltMap Value if key exists
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

    // Helper to safely get a map value from a BoltMap Value if key exists
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

    BoltError deserialize_hello_message_request(PackStreamReader& reader, HelloMessageParams& out_params, const versions::Version& server_negotiated_version) {
        if (reader.has_error()) return reader.get_error();
        // Clear out_params members
        out_params = {};  // Default construct then fill

        PackStreamStructure hello_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::HELLO, 1, 1, hello_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }

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
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);  // Map field was null shared_ptr
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        const auto& extra_map_pairs = extra_map_sptr->pairs;

        // User agent (mandatory in practice)
        auto ua_it = extra_map_pairs.find("user_agent");
        if (ua_it != extra_map_pairs.end() && std::holds_alternative<std::string>(ua_it->second)) {
            out_params.user_agent = std::get<std::string>(ua_it->second);
        } else {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);  // user_agent is effectively mandatory
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        // Authentication fields (relevant if server_negotiated_version < 5.1)
        bool expect_auth_in_hello = (server_negotiated_version.major < 5) || (server_negotiated_version.major == 5 && server_negotiated_version.minor < 1);
        if (expect_auth_in_hello) {
            out_params.auth_scheme = get_optional_string_from_map(*extra_map_sptr, "scheme");
            out_params.auth_principal = get_optional_string_from_map(*extra_map_sptr, "principal");
            out_params.auth_credentials = get_optional_string_from_map(*extra_map_sptr, "credentials");
            // For auth_scheme_specific_tokens, one would iterate extra_map_pairs and collect non-standard keys
            // or have a predefined list of keys for known complex schemes.
            // This simplified version doesn't populate auth_scheme_specific_tokens.
        }

        // Routing context (Bolt 4.1+)
        if (server_negotiated_version.major > 4 || (server_negotiated_version.major == 4 && server_negotiated_version.minor >= 1)) {
            out_params.routing_context = get_optional_map_from_map(*extra_map_sptr, "routing");
        }

        // Patch Bolt (Bolt 4.3 - 4.4)
        if (server_negotiated_version.major == 4 && (server_negotiated_version.minor == 3 || server_negotiated_version.minor == 4)) {
            out_params.patch_bolt = get_optional_list_string_from_map(*extra_map_sptr, "patch_bolt");
        }

        // Notification configuration (Bolt 5.2+)
        if (server_negotiated_version.major > 5 || (server_negotiated_version.major == 5 && server_negotiated_version.minor >= 2)) {
            out_params.notifications_min_severity = get_optional_string_from_map(*extra_map_sptr, "notifications_minimum_severity");
            out_params.notifications_disabled_categories = get_optional_list_string_from_map(*extra_map_sptr, "notifications_disabled_categories");
        }

        // Bolt Agent information (Bolt 5.3+)
        bool bolt_agent_expected = (server_negotiated_version.major > 5 || (server_negotiated_version.major == 5 && server_negotiated_version.minor >= 3));
        auto bolt_agent_map_val_it = extra_map_pairs.find("bolt_agent");
        if (bolt_agent_map_val_it != extra_map_pairs.end() && std::holds_alternative<std::shared_ptr<BoltMap>>(bolt_agent_map_val_it->second)) {
            auto agent_map_sptr = std::get<std::shared_ptr<BoltMap>>(bolt_agent_map_val_it->second);
            if (agent_map_sptr) {
                HelloMessageParams::BoltAgentInfo agent_info;
                auto product_val = get_optional_string_from_map(*agent_map_sptr, "product");
                if (!product_val.has_value() || product_val.value().empty()) {
                    if (bolt_agent_expected) {  // Product is mandatory if bolt_agent is present and expected
                        reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                        return BoltError::INVALID_MESSAGE_FORMAT;
                    }
                } else {
                    agent_info.product = product_val.value();
                }

                agent_info.platform = get_optional_string_from_map(*agent_map_sptr, "platform");
                agent_info.language = get_optional_string_from_map(*agent_map_sptr, "language");
                agent_info.language_details = get_optional_string_from_map(*agent_map_sptr, "language_details");

                if (!agent_info.product.empty()) {  // Only set bolt_agent if product was found
                    out_params.bolt_agent = agent_info;
                } else if (bolt_agent_expected) {  // Product was empty but agent was expected
                    reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                    return BoltError::INVALID_MESSAGE_FORMAT;
                }
            } else if (bolt_agent_expected) {  // bolt_agent key exists but points to a null map
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
        } else if (bolt_agent_expected) {  // bolt_agent key missing or not a map, but was mandatory
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        // Store other_extra_tokens: iterate extra_map_pairs and add any keys not handled above.
        // This requires knowing all standard keys.
        // For simplicity, this step is omitted here but would be needed for full compliance.
        // out_params.other_extra_tokens = ... ;

        return BoltError::SUCCESS;
    }

    // deserialize_run_message_request remains unchanged from its previous location.
    BoltError deserialize_run_message_request(PackStreamReader& reader, RunMessageParams& out_params) {
        if (reader.has_error()) return reader.get_error();
        out_params.cypher_query.clear();
        out_params.parameters.clear();
        out_params.extra_metadata.clear();

        PackStreamStructure run_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::RUN, 2, 3, run_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }

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
        std::shared_ptr<BoltMap> params_map_sptr;
        try {
            params_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_contents.fields[1]));
        } catch (const std::bad_variant_access&) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
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

        if (run_struct_contents.fields.size() == 3) {
            if (!std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct_contents.fields[2])) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
            std::shared_ptr<BoltMap> extra_map_sptr;
            try {
                extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_contents.fields[2]));
            } catch (const std::bad_variant_access&) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            } catch (const std::exception&) {
                reader.set_error(BoltError::UNKNOWN_ERROR);
                return BoltError::UNKNOWN_ERROR;
            }

            if (extra_map_sptr) {
                try {
                    out_params.extra_metadata = std::move(extra_map_sptr->pairs);
                } catch (const std::bad_alloc&) {
                    reader.set_error(BoltError::OUT_OF_MEMORY);
                    return BoltError::OUT_OF_MEMORY;
                } catch (const std::exception&) {
                    reader.set_error(BoltError::UNKNOWN_ERROR);
                    return BoltError::UNKNOWN_ERROR;
                }
            } else {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol