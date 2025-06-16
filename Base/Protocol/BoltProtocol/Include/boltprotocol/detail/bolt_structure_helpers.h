#ifndef BOLTPROTOCOL_DETAIL_STRUCTURE_HELPERS_H
#define BOLTPROTOCOL_DETAIL_STRUCTURE_HELPERS_H

#include <map>
#include <memory>  // For std::shared_ptr
#include <optional>
#include <string>
#include <variant>  // For std::holds_alternative, std::get
#include <vector>

#include "boltprotocol/bolt_core_types.h"       // For Value, BoltList, BoltMap, PackStreamStructure
#include "boltprotocol/bolt_errors_versions.h"  // For versions::Version, BoltError
#include "boltprotocol/bolt_structure_types.h"  // For forward declaring BoltNode etc. if needed, or full defs for recursion

// Forward declare from_packstream for recursive calls in get_typed_list_field
namespace boltprotocol {
    // We need to forward declare all from_packstream overloads that might be called recursively
    BoltError from_packstream(const PackStreamStructure& pss, BoltNode& out_node, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltRelationship& out_rel, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltUnboundRelationship& out_unbound_rel, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltPath& out_path, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltDate& out_date);
    BoltError from_packstream(const PackStreamStructure& pss, BoltTime& out_time);
    BoltError from_packstream(const PackStreamStructure& pss, BoltLocalTime& out_local_time);
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTime& out_datetime, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTimeZoneId& out_datetime_zoneid, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltLocalDateTime& out_local_datetime);
    BoltError from_packstream(const PackStreamStructure& pss, BoltDuration& out_duration);
    BoltError from_packstream(const PackStreamStructure& pss, BoltPoint2D& out_point);
    BoltError from_packstream(const PackStreamStructure& pss, BoltPoint3D& out_point);
}  // namespace boltprotocol

namespace boltprotocol {
    namespace detail {

        template <typename T>
        inline std::optional<T> get_typed_field(const std::vector<Value>& fields, size_t index) {
            if (index < fields.size()) {
                const Value& field_value = fields[index];
                if (std::holds_alternative<T>(field_value)) {
                    try {
                        return std::get<T>(field_value);
                    } catch (const std::bad_variant_access&) { /* Defensive */
                    }
                }
            }
            return std::nullopt;
        }

        // Specialization for shared_ptr<BoltMap> to return the inner map directly
        template <>
        inline std::optional<std::map<std::string, Value>> get_typed_field<std::map<std::string, Value>>(const std::vector<Value>& fields, size_t index) {
            if (index < fields.size()) {
                const Value& field_value = fields[index];
                if (std::holds_alternative<std::shared_ptr<BoltMap>>(field_value)) {
                    const auto& map_sptr = std::get<std::shared_ptr<BoltMap>>(field_value);
                    if (map_sptr) {
                        try {
                            return map_sptr->pairs;
                        } catch (...) { /* map copy failed */
                        }
                    }
                }
            }
            return std::nullopt;
        }

        template <typename T>  // T is the target strong type, e.g., BoltNode
        inline std::optional<std::vector<T>> get_typed_list_field(const std::vector<Value>& fields, size_t index, const versions::Version* bolt_version_for_nested = nullptr) {
            if (index < fields.size()) {
                const Value& field_value = fields[index];
                if (std::holds_alternative<std::shared_ptr<BoltList>>(field_value)) {
                    const auto& list_sptr = std::get<std::shared_ptr<BoltList>>(field_value);
                    if (list_sptr) {
                        std::vector<T> result;
                        result.reserve(list_sptr->elements.size());
                        bool conversion_ok = true;
                        for (const auto& list_element_value : list_sptr->elements) {
                            if (std::holds_alternative<std::shared_ptr<PackStreamStructure>>(list_element_value)) {
                                const auto& element_pss_sptr = std::get<std::shared_ptr<PackStreamStructure>>(list_element_value);
                                if (element_pss_sptr) {
                                    T typed_element;
                                    BoltError err = BoltError::UNKNOWN_ERROR;  // Initialize to an error state

                                    // Dispatch based on type T for version parameter
                                    if constexpr (std::is_same_v<T, BoltNode> || std::is_same_v<T, BoltRelationship> || std::is_same_v<T, BoltUnboundRelationship> || std::is_same_v<T, BoltPath> || std::is_same_v<T, BoltDateTime> || std::is_same_v<T, BoltDateTimeZoneId>) {
                                        if (!bolt_version_for_nested) {  // Version is required for these types
                                            conversion_ok = false;
                                            break;
                                        }
                                        err = from_packstream(*element_pss_sptr, typed_element, *bolt_version_for_nested);
                                    } else {  // For types like BoltDate, BoltTime, etc., that don't need version for from_packstream
                                        err = from_packstream(*element_pss_sptr, typed_element);
                                    }

                                    if (err == BoltError::SUCCESS) {
                                        try {
                                            result.push_back(std::move(typed_element));
                                        } catch (...) {
                                            conversion_ok = false;
                                            break;
                                        }
                                    } else {
                                        conversion_ok = false;
                                        break;
                                    }
                                } else {
                                    conversion_ok = false;
                                    break;
                                }  // Null PSS in list
                            } else {
                                conversion_ok = false;
                                break;
                            }  // Element not a PSS
                        }
                        if (conversion_ok) return result;
                    }
                }
            }
            return std::nullopt;
        }
    }  // namespace detail
}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_DETAIL_STRUCTURE_HELPERS_H