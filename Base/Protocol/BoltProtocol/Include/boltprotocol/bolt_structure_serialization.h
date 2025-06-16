#ifndef BOLTPROTOCOL_STRUCTURE_SERIALIZATION_H
#define BOLTPROTOCOL_STRUCTURE_SERIALIZATION_H

#include "bolt_core_types.h"       // For PackStreamStructure, Value
#include "bolt_errors_versions.h"  // For BoltError, versions::Version
#include "bolt_structure_types.h"  // For BoltNode, BoltRelationship, etc.

namespace boltprotocol {

    // --- Conversion from PackStreamStructure to Typed Struct ---

    BoltError from_packstream(const PackStreamStructure& pss, BoltNode& out_node, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltRelationship& out_rel, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltUnboundRelationship& out_unbound_rel, const versions::Version& bolt_version);
    BoltError from_packstream(const PackStreamStructure& pss, BoltPath& out_path, const versions::Version& bolt_version);

    BoltError from_packstream(const PackStreamStructure& pss, BoltDate& out_date);
    BoltError from_packstream(const PackStreamStructure& pss, BoltTime& out_time);
    BoltError from_packstream(const PackStreamStructure& pss, BoltLocalTime& out_local_time);
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTime& out_datetime, const versions::Version& bolt_version);               // Version for legacy handling
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTimeZoneId& out_datetime_zoneid, const versions::Version& bolt_version);  // Version for legacy handling
    BoltError from_packstream(const PackStreamStructure& pss, BoltLocalDateTime& out_local_datetime);
    BoltError from_packstream(const PackStreamStructure& pss, BoltDuration& out_duration);

    BoltError from_packstream(const PackStreamStructure& pss, BoltPoint2D& out_point);
    BoltError from_packstream(const PackStreamStructure& pss, BoltPoint3D& out_point);

    // --- Conversion from Typed Struct to PackStreamStructure (for client sending these types) ---
    // These would create a std::shared_ptr<PackStreamStructure> suitable for putting into a Value.

    BoltError to_packstream(const BoltNode& node, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltRelationship& rel, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltUnboundRelationship& unbound_rel, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltPath& path, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);

    BoltError to_packstream(const BoltDate& date, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltTime& time, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltLocalTime& local_time, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltDateTime& datetime, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltDateTimeZoneId& datetime_zoneid, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltLocalDateTime& local_datetime, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltDuration& duration, std::shared_ptr<PackStreamStructure>& out_pss);

    BoltError to_packstream(const BoltPoint2D& point, std::shared_ptr<PackStreamStructure>& out_pss);
    BoltError to_packstream(const BoltPoint3D& point, std::shared_ptr<PackStreamStructure>& out_pss);

    // Convenience template to extract from Value if it holds a PackStreamStructure
    template <typename T>
    BoltError value_to_typed_struct(const Value& value, T& out_typed_struct, const versions::Version& bolt_version) {
        if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(value)) {
            return BoltError::INVALID_MESSAGE_FORMAT;  // Or a more specific type mismatch error
        }
        const auto& pss_sptr = std::get<std::shared_ptr<PackStreamStructure>>(value);
        if (!pss_sptr) {
            return BoltError::INVALID_MESSAGE_FORMAT;  // Null PSS pointer
        }
        return from_packstream(*pss_sptr, out_typed_struct, bolt_version);
    }

    // Overload for types not needing bolt_version for deserialization
    template <typename T>
    BoltError value_to_typed_struct(const Value& value, T& out_typed_struct) {
        if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(value)) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        const auto& pss_sptr = std::get<std::shared_ptr<PackStreamStructure>>(value);
        if (!pss_sptr) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        return from_packstream(*pss_sptr, out_typed_struct);
    }

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_STRUCTURE_SERIALIZATION_H