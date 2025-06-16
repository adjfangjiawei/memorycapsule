#ifndef BOLTPROTOCOL_STRUCTURE_SERIALIZATION_H
#define BOLTPROTOCOL_STRUCTURE_SERIALIZATION_H

#include "bolt_core_types.h"
#include "bolt_errors_versions.h"
#include "bolt_structure_types.h"

namespace boltprotocol {

    // --- Conversion from PackStreamStructure to Typed Struct ---

    BoltError from_packstream(const PackStreamStructure& pss, BoltNode& out_node, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltRelationship& out_rel, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltUnboundRelationship& out_unbound_rel, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltPath& out_path, const versions::Version& bolt_version);

    BoltError from_packstream(const PackStreamStructure& pss, BoltDate& out_date);
    BoltError from_packstream(const PackStreamStructure& pss, BoltTime& out_time);
    BoltError from_packstream(const PackStreamStructure& pss, BoltLocalTime& out_local_time);
    // For from_packstream, utc_patch_active might not be strictly needed if we rely on the tag ('I' vs 'F') primarily.
    // However, for to_packstream, it's crucial for Bolt 4.4.
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTime& out_datetime, const versions::Version& bolt_version /*, bool utc_patch_active_for_4_4 = false (tag driven)*/);
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTimeZoneId& out_datetime_zoneid, const versions::Version& bolt_version /*, bool utc_patch_active_for_4_4 = false (tag driven)*/);
    BoltError from_packstream(const PackStreamStructure& pss, BoltLocalDateTime& out_local_datetime);
    BoltError from_packstream(const PackStreamStructure& pss, BoltDuration& out_duration);

    BoltError from_packstream(const PackStreamStructure& pss, BoltPoint2D& out_point);
    BoltError from_packstream(const PackStreamStructure& pss, BoltPoint3D& out_point);

    // --- Conversion from Typed Struct to PackStreamStructure ---

    BoltError to_packstream(const BoltNode& node, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltRelationship& rel, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltUnboundRelationship& unbound_rel, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltPath& path, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);

    BoltError to_packstream(const BoltDate& date, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltTime& time, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltLocalTime& local_time, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltDateTime& datetime, const versions::Version& bolt_version, bool utc_patch_active_for_4_4, std::shared_ptr<PackStreamStructure>& out_pss);               // <--- MODIFIED
    BoltError to_packstream(const BoltDateTimeZoneId& datetime_zoneid, const versions::Version& bolt_version, bool utc_patch_active_for_4_4, std::shared_ptr<PackStreamStructure>& out_pss);  // <--- MODIFIED
    BoltError to_packstream(const BoltLocalDateTime& local_datetime, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltDuration& duration, std::shared_ptr<PackStreamStructure>& out_pss);

    BoltError to_packstream(const BoltPoint2D& point, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltPoint3D& point, std::shared_ptr<PackStreamStructure>& out_pss);

    // Convenience template to extract from Value
    template <typename T>
    BoltError value_to_typed_struct(const Value& value, T& out_typed_struct, const versions::Version& bolt_version, bool utc_patch_active_for_4_4 = false) {
        if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(value)) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        const auto& pss_sptr = std::get<std::shared_ptr<PackStreamStructure>>(value);
        if (!pss_sptr) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        // Dispatch to the correct from_packstream overload based on T
        if constexpr (std::is_same_v<T, BoltDateTime> || std::is_same_v<T, BoltDateTimeZoneId> || std::is_same_v<T, BoltNode> || std::is_same_v<T, BoltRelationship> || std::is_same_v<T, BoltUnboundRelationship> || std::is_same_v<T, BoltPath>) {
            // These types' from_packstream take bolt_version.
            // For DateTime/DateTimeZoneId, utc_patch_active is not directly used by from_packstream (tag driven).
            return from_packstream(*pss_sptr, out_typed_struct, bolt_version);
        } else {
            // For types like BoltDate, BoltTime, etc., that don't need bolt_version for from_packstream.
            return from_packstream(*pss_sptr, out_typed_struct);
        }
    }
    // Keep the overload for types not needing version or patch info for deserialization (tag-driven)
    template <typename T>
    BoltError value_to_typed_struct(const Value& value, T& out_typed_struct) {
        if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(value)) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        const auto& pss_sptr = std::get<std::shared_ptr<PackStreamStructure>>(value);
        if (!pss_sptr) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        // This simple overload is suitable for types whose from_packstream does not need a version.
        // For version-dependent types, the other overload must be used.
        return from_packstream(*pss_sptr, out_typed_struct);
    }

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_STRUCTURE_SERIALIZATION_H