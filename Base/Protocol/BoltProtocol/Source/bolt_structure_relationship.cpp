#include <variant>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/detail/bolt_structure_helpers.h"

namespace boltprotocol {

    BoltError from_packstream(const PackStreamStructure& pss, BoltRelationship& out_rel, const versions::Version& bolt_version) {
        if (pss.tag != 0x52) return BoltError::INVALID_MESSAGE_FORMAT;

        size_t expected_fields_min = 5;
        size_t expected_fields_max = (bolt_version.major >= 5) ? 8 : 5;

        if (pss.fields.size() < expected_fields_min || pss.fields.size() > expected_fields_max) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        auto id_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        auto start_id_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
        auto end_id_opt = detail::get_typed_field<int64_t>(pss.fields, 2);
        auto type_opt = detail::get_typed_field<std::string>(pss.fields, 3);
        auto props_map_opt = detail::get_typed_field<std::map<std::string, Value>>(pss.fields, 4);

        if (!id_opt || !start_id_opt || !end_id_opt || !type_opt || !props_map_opt) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        out_rel.id = id_opt.value();
        out_rel.start_node_id = start_id_opt.value();
        out_rel.end_node_id = end_id_opt.value();
        out_rel.type = type_opt.value();
        try {
            out_rel.properties = props_map_opt.value();
        } catch (...) {
            return BoltError::OUT_OF_MEMORY;
        }

        if (bolt_version.major >= 5 && pss.fields.size() == 8) {
            out_rel.element_id = detail::get_typed_field<std::string>(pss.fields, 5);
            out_rel.start_node_element_id = detail::get_typed_field<std::string>(pss.fields, 6);
            out_rel.end_node_element_id = detail::get_typed_field<std::string>(pss.fields, 7);
        } else {
            out_rel.element_id = std::nullopt;
            out_rel.start_node_element_id = std::nullopt;
            out_rel.end_node_element_id = std::nullopt;
        }
        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltRelationship& rel, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x52;
            out_pss_sptr->fields.emplace_back(Value(rel.id));
            out_pss_sptr->fields.emplace_back(Value(rel.start_node_id));
            out_pss_sptr->fields.emplace_back(Value(rel.end_node_id));
            out_pss_sptr->fields.emplace_back(Value(rel.type));
            auto props_map_sptr = std::make_shared<BoltMap>();
            props_map_sptr->pairs = rel.properties;
            out_pss_sptr->fields.emplace_back(Value(props_map_sptr));

            if (bolt_version.major >= 5) {
                out_pss_sptr->fields.emplace_back(rel.element_id.has_value() ? Value(rel.element_id.value()) : nullptr);
                out_pss_sptr->fields.emplace_back(rel.start_node_element_id.has_value() ? Value(rel.start_node_element_id.value()) : nullptr);
                out_pss_sptr->fields.emplace_back(rel.end_node_element_id.has_value() ? Value(rel.end_node_element_id.value()) : nullptr);
            }
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

    BoltError from_packstream(const PackStreamStructure& pss, BoltUnboundRelationship& out_unbound_rel, const versions::Version& bolt_version) {
        if (pss.tag != 0x72) return BoltError::INVALID_MESSAGE_FORMAT;
        size_t expected_fields_min = 3;
        size_t expected_fields_max = (bolt_version.major >= 5) ? 4 : 3;
        if (pss.fields.size() < expected_fields_min || pss.fields.size() > expected_fields_max) return BoltError::INVALID_MESSAGE_FORMAT;

        auto id_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        auto type_opt = detail::get_typed_field<std::string>(pss.fields, 1);
        auto props_map_opt = detail::get_typed_field<std::map<std::string, Value>>(pss.fields, 2);

        if (!id_opt || !type_opt || !props_map_opt) return BoltError::INVALID_MESSAGE_FORMAT;

        out_unbound_rel.id = id_opt.value();
        out_unbound_rel.type = type_opt.value();
        try {
            out_unbound_rel.properties = props_map_opt.value();
        } catch (...) {
            return BoltError::OUT_OF_MEMORY;
        }

        if (bolt_version.major >= 5 && pss.fields.size() == 4) {
            out_unbound_rel.element_id = detail::get_typed_field<std::string>(pss.fields, 3);
        } else {
            out_unbound_rel.element_id = std::nullopt;
        }
        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltUnboundRelationship& unbound_rel, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x72;
            out_pss_sptr->fields.emplace_back(Value(unbound_rel.id));
            out_pss_sptr->fields.emplace_back(Value(unbound_rel.type));
            auto props_map_sptr = std::make_shared<BoltMap>();
            props_map_sptr->pairs = unbound_rel.properties;
            out_pss_sptr->fields.emplace_back(Value(props_map_sptr));
            if (bolt_version.major >= 5) {
                out_pss_sptr->fields.emplace_back(unbound_rel.element_id.has_value() ? Value(unbound_rel.element_id.value()) : nullptr);
            }
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol