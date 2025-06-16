#include <variant>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/detail/bolt_structure_helpers.h"  // For get_typed_field etc.

namespace boltprotocol {

    BoltError from_packstream(const PackStreamStructure& pss, BoltNode& out_node, const versions::Version& bolt_version) {
        if (pss.tag != 0x4E) return BoltError::INVALID_MESSAGE_FORMAT;

        size_t expected_fields_min = 3;
        size_t expected_fields_max = (bolt_version.major >= 5) ? 4 : 3;

        if (pss.fields.size() < expected_fields_min || pss.fields.size() > expected_fields_max) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        auto id_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        auto labels_list_sptr_opt = detail::get_typed_field<std::shared_ptr<BoltList>>(pss.fields, 1);
        auto props_map_opt = detail::get_typed_field<std::map<std::string, Value>>(pss.fields, 2);  // Uses specialization

        if (!id_opt.has_value() || !labels_list_sptr_opt.has_value() || !props_map_opt.has_value()) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        const auto& labels_list_sptr = labels_list_sptr_opt.value();
        if (!labels_list_sptr) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        out_node.id = id_opt.value();

        out_node.labels.clear();
        out_node.labels.reserve(labels_list_sptr->elements.size());
        for (const auto& label_val : labels_list_sptr->elements) {
            if (std::holds_alternative<std::string>(label_val)) {
                try {
                    out_node.labels.push_back(std::get<std::string>(label_val));
                } catch (...) {
                    return BoltError::OUT_OF_MEMORY;
                }  // Or UNKNOWN_ERROR
            } else {
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
        }

        try {
            out_node.properties = props_map_opt.value();
        } catch (...) {
            return BoltError::OUT_OF_MEMORY;
        }

        if (bolt_version.major >= 5 && pss.fields.size() == 4) {
            out_node.element_id = detail::get_typed_field<std::string>(pss.fields, 3);
            // If field 3 is present but not string, get_typed_field returns nullopt, element_id remains nullopt.
            // If it's present and is PackNull, it should also result in nullopt.
            // This needs to be handled carefully if PackNull should clear the optional vs. type mismatch.
            // Current get_typed_field will return nullopt if type is not string.
            if (pss.fields[3].index() != 0 && !out_node.element_id.has_value() && pss.fields[3].index() != detail::get_typed_field<std::string>(pss.fields, 3).has_value()) {
                // This condition means: field 3 is not PackNull, AND we didn't get a string, AND it wasn't because it was a string.
                // This is a bit complex, usually indicates a type mismatch that wasn't caught by holds_alternative if Value was more complex.
                // For string, if it's not string, get_typed_field returns nullopt.
            }
        } else {
            out_node.element_id = std::nullopt;
        }

        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltNode& node, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x4E;  // 'N'

            out_pss_sptr->fields.emplace_back(Value(node.id));

            auto labels_list_sptr = std::make_shared<BoltList>();
            labels_list_sptr->elements.reserve(node.labels.size());
            for (const auto& label : node.labels) {
                labels_list_sptr->elements.emplace_back(Value(label));
            }
            out_pss_sptr->fields.emplace_back(Value(labels_list_sptr));

            auto props_map_sptr = std::make_shared<BoltMap>();
            props_map_sptr->pairs = node.properties;
            out_pss_sptr->fields.emplace_back(Value(props_map_sptr));

            if (bolt_version.major >= 5) {
                if (node.element_id.has_value()) {
                    out_pss_sptr->fields.emplace_back(Value(node.element_id.value()));
                } else {
                    out_pss_sptr->fields.emplace_back(nullptr);
                }
            }
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol