#include <exception>
#include <map>
#include <memory>
#include <optional>  // For std::optional
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError serialize_hello_message(const HelloMessageParams& params, PackStreamWriter& writer, const versions::Version& client_target_version) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure pss_hello_obj;
        pss_hello_obj.tag = static_cast<uint8_t>(MessageTag::HELLO);

        std::shared_ptr<BoltMap> extra_map_sptr;
        try {
            extra_map_sptr = std::make_shared<BoltMap>();
            auto& pairs = extra_map_sptr->pairs;

            // User agent (always present)
            pairs.emplace("user_agent", Value(params.user_agent));

            // Authentication fields (if Bolt < 5.1 or for INIT message semantics if client_target_version indicates that)
            // Client needs to decide based on its target protocol version strategy.
            // If targeting Bolt 5.1+, these auth fields should be in LOGON.
            bool use_auth_in_hello = (client_target_version.major < 5) || (client_target_version.major == 5 && client_target_version.minor < 1);

            if (use_auth_in_hello) {
                if (params.auth_scheme.has_value()) {
                    pairs.emplace("scheme", Value(params.auth_scheme.value()));
                }
                if (params.auth_principal.has_value()) {
                    pairs.emplace("principal", Value(params.auth_principal.value()));
                }
                if (params.auth_credentials.has_value()) {
                    pairs.emplace("credentials", Value(params.auth_credentials.value()));
                }
                if (params.auth_scheme_specific_tokens.has_value()) {
                    for (const auto& token_pair : params.auth_scheme_specific_tokens.value()) {
                        pairs.emplace(token_pair.first, token_pair.second);  // Copy Value
                    }
                }
            } else {
                // For Bolt 5.1+, if these fields are provided in HelloMessageParams, it's a misuse by caller.
                // Could add a warning or error here, or assume caller knows what they're doing.
                // For now, we just don't serialize them into HELLO if targeting 5.1+.
            }

            // Routing context (Bolt 4.1+)
            if (client_target_version.major > 4 || (client_target_version.major == 4 && client_target_version.minor >= 1)) {
                if (params.routing_context.has_value()) {
                    // The routing context itself is a map. It's wrapped in a Value.
                    auto routing_map_val_sptr = std::make_shared<BoltMap>();
                    routing_map_val_sptr->pairs = params.routing_context.value();
                    pairs.emplace("routing", Value(routing_map_val_sptr));
                } else {
                    // Per spec, if routing key is present with null value, server should not carry out routing.
                    // If key is absent, server uses default. If client wants to explicitly disable routing,
                    // it should send {"routing": null}.
                    // We can choose to always send "routing": null if params.routing_context is not set,
                    // or omit the "routing" key entirely. Omitting is simpler if no explicit disable is intended.
                    // If an explicit `{"routing": null}` is desired, params.routing_context should contain that.
                }
            }

            // Patch Bolt (Bolt 4.3 - 4.4)
            if (client_target_version.major == 4 && (client_target_version.minor == 3 || client_target_version.minor == 4)) {
                if (params.patch_bolt.has_value() && !params.patch_bolt.value().empty()) {
                    auto patch_list_sptr = std::make_shared<BoltList>();
                    for (const auto& patch_str : params.patch_bolt.value()) {
                        patch_list_sptr->elements.emplace_back(Value(patch_str));
                    }
                    pairs.emplace("patch_bolt", Value(patch_list_sptr));
                }
            }

            // Notification configuration (Bolt 5.2+)
            if (client_target_version.major > 5 || (client_target_version.major == 5 && client_target_version.minor >= 2)) {
                if (params.notifications_min_severity.has_value()) {
                    pairs.emplace("notifications_minimum_severity", Value(params.notifications_min_severity.value()));
                }
                if (params.notifications_disabled_categories.has_value() && !params.notifications_disabled_categories.value().empty()) {
                    auto disabled_cat_list_sptr = std::make_shared<BoltList>();
                    for (const auto& cat_str : params.notifications_disabled_categories.value()) {
                        disabled_cat_list_sptr->elements.emplace_back(Value(cat_str));
                    }
                    pairs.emplace("notifications_disabled_categories", Value(disabled_cat_list_sptr));
                }
            }

            // Bolt Agent information (Bolt 5.3+, mandatory)
            bool bolt_agent_mandatory = (client_target_version.major > 5 || (client_target_version.major == 5 && client_target_version.minor >= 3));
            if (bolt_agent_mandatory) {
                if (!params.bolt_agent.has_value() || params.bolt_agent.value().product.empty()) {
                    writer.set_error(BoltError::SERIALIZATION_ERROR);  // Bolt agent product is mandatory
                    return BoltError::SERIALIZATION_ERROR;
                }
                auto bolt_agent_map_sptr = std::make_shared<BoltMap>();
                const auto& agent_info = params.bolt_agent.value();
                bolt_agent_map_sptr->pairs.emplace("product", Value(agent_info.product));
                if (agent_info.platform.has_value()) {
                    bolt_agent_map_sptr->pairs.emplace("platform", Value(agent_info.platform.value()));
                }
                if (agent_info.language.has_value()) {
                    bolt_agent_map_sptr->pairs.emplace("language", Value(agent_info.language.value()));
                }
                if (agent_info.language_details.has_value()) {
                    bolt_agent_map_sptr->pairs.emplace("language_details", Value(agent_info.language_details.value()));
                }
                pairs.emplace("bolt_agent", Value(bolt_agent_map_sptr));
            } else if (params.bolt_agent.has_value()) {  // Optional if not mandatory, serialize if provided
                auto bolt_agent_map_sptr = std::make_shared<BoltMap>();
                const auto& agent_info = params.bolt_agent.value();
                bolt_agent_map_sptr->pairs.emplace("product", Value(agent_info.product));
                if (agent_info.platform.has_value()) bolt_agent_map_sptr->pairs.emplace("platform", Value(agent_info.platform.value()));
                if (agent_info.language.has_value()) bolt_agent_map_sptr->pairs.emplace("language", Value(agent_info.language.value()));
                if (agent_info.language_details.has_value()) bolt_agent_map_sptr->pairs.emplace("language_details", Value(agent_info.language_details.value()));
                pairs.emplace("bolt_agent", Value(bolt_agent_map_sptr));
            }

            // Other custom tokens
            for (const auto& token_pair : params.other_extra_tokens) {
                // Be careful not to overwrite keys already set by specific fields above.
                // emplace only inserts if key doesn't exist. Use insert_or_assign or check first if overwrite is possible/intended.
                // For simplicity, assuming other_extra_tokens don't clash with standard keys.
                pairs.emplace(token_pair.first, token_pair.second);
            }

            pss_hello_obj.fields.emplace_back(Value(extra_map_sptr));

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pss_hello_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    // serialize_goodbye_message and serialize_reset_message remain unchanged from their previous location.
    // They are simple and don't depend on HelloMessageParams changes.

    BoltError serialize_goodbye_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::GOODBYE);
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_reset_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::RESET);
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    // Implementations for serialize_logon_message and serialize_logoff_message will go here
    BoltError serialize_logon_message(const LogonMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure pss_logon_obj;
        pss_logon_obj.tag = static_cast<uint8_t>(MessageTag::LOGON);

        std::shared_ptr<BoltMap> auth_map_sptr;
        try {
            auth_map_sptr = std::make_shared<BoltMap>();
            auth_map_sptr->pairs = params.auth_tokens;  // map copy

            pss_logon_obj.fields.emplace_back(Value(auth_map_sptr));

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pss_logon_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_logoff_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::LOGOFF);
            // LOGOFF has no fields in its PSS
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

}  // namespace boltprotocol