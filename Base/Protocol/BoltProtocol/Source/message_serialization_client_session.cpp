#include <exception>
#include <map>
#include <memory>
#include <optional>
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

            pairs.emplace("user_agent", Value(params.user_agent));

            bool use_auth_in_hello = (client_target_version < versions::V5_1);  // Qualified comparison

            if (use_auth_in_hello) {
                if (params.auth_scheme.has_value()) pairs.emplace("scheme", Value(params.auth_scheme.value()));
                if (params.auth_principal.has_value()) pairs.emplace("principal", Value(params.auth_principal.value()));
                if (params.auth_credentials.has_value()) pairs.emplace("credentials", Value(params.auth_credentials.value()));
                if (params.auth_scheme_specific_tokens.has_value()) {
                    for (const auto& token_pair : params.auth_scheme_specific_tokens.value()) {
                        pairs.emplace(token_pair.first, token_pair.second);
                    }
                }
            }

            if (client_target_version.major > 4 || (client_target_version.major == 4 && client_target_version.minor >= 1)) {
                if (params.routing_context.has_value()) {
                    auto routing_map_val_sptr = std::make_shared<BoltMap>();
                    routing_map_val_sptr->pairs = params.routing_context.value();
                    pairs.emplace("routing", Value(routing_map_val_sptr));
                }
            }

            if (client_target_version.major == 4 && (client_target_version.minor == 3 || client_target_version.minor == 4)) {
                if (params.patch_bolt.has_value() && !params.patch_bolt.value().empty()) {
                    auto patch_list_sptr = std::make_shared<BoltList>();
                    for (const auto& patch_str : params.patch_bolt.value()) {
                        patch_list_sptr->elements.emplace_back(Value(patch_str));
                    }
                    pairs.emplace("patch_bolt", Value(patch_list_sptr));
                }
            }

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

            bool bolt_agent_mandatory = (client_target_version.major > 5 || (client_target_version.major == 5 && client_target_version.minor >= 3));
            if (bolt_agent_mandatory) {
                if (!params.bolt_agent.has_value() || params.bolt_agent.value().product.empty()) {
                    writer.set_error(BoltError::SERIALIZATION_ERROR);
                    return BoltError::SERIALIZATION_ERROR;
                }
                auto bolt_agent_map_sptr = std::make_shared<BoltMap>();
                const auto& agent_info = params.bolt_agent.value();
                bolt_agent_map_sptr->pairs.emplace("product", Value(agent_info.product));
                if (agent_info.platform.has_value()) bolt_agent_map_sptr->pairs.emplace("platform", Value(agent_info.platform.value()));
                if (agent_info.language.has_value()) bolt_agent_map_sptr->pairs.emplace("language", Value(agent_info.language.value()));
                if (agent_info.language_details.has_value()) bolt_agent_map_sptr->pairs.emplace("language_details", Value(agent_info.language_details.value()));
                pairs.emplace("bolt_agent", Value(bolt_agent_map_sptr));
            } else if (params.bolt_agent.has_value()) {
                auto bolt_agent_map_sptr = std::make_shared<BoltMap>();
                const auto& agent_info = params.bolt_agent.value();
                bolt_agent_map_sptr->pairs.emplace("product", Value(agent_info.product));  // Product is mandatory in BoltAgentInfo itself
                if (agent_info.platform.has_value()) bolt_agent_map_sptr->pairs.emplace("platform", Value(agent_info.platform.value()));
                if (agent_info.language.has_value()) bolt_agent_map_sptr->pairs.emplace("language", Value(agent_info.language.value()));
                if (agent_info.language_details.has_value()) bolt_agent_map_sptr->pairs.emplace("language_details", Value(agent_info.language_details.value()));
                pairs.emplace("bolt_agent", Value(bolt_agent_map_sptr));
            }

            for (const auto& token_pair : params.other_extra_tokens) {
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

    BoltError serialize_logon_message(const LogonMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure pss_logon_obj;
        pss_logon_obj.tag = static_cast<uint8_t>(MessageTag::LOGON);

        std::shared_ptr<BoltMap> auth_map_sptr;
        try {
            auth_map_sptr = std::make_shared<BoltMap>();
            // The auth_tokens map in LogonMessageParams directly becomes the PSS field.
            auth_map_sptr->pairs = params.auth_tokens;

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
            // LOGOFF PSS has no fields.
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