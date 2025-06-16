#include <variant>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/detail/bolt_structure_helpers.h"

namespace boltprotocol {

    BoltError from_packstream(const PackStreamStructure& pss, BoltTime& out_time) {
        if (pss.tag != 0x54) return BoltError::INVALID_MESSAGE_FORMAT;  // 'T'
        if (pss.fields.size() != 2) return BoltError::INVALID_MESSAGE_FORMAT;

        auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        auto offset_opt = detail::get_typed_field<int64_t>(pss.fields, 1);  // Spec: Integer

        if (!nanos_opt.has_value() || !offset_opt.has_value()) return BoltError::INVALID_MESSAGE_FORMAT;

        out_time.nanoseconds_since_midnight = nanos_opt.value();
        // tz_offset_seconds is int32_t in BoltTime struct. Check for overflow if necessary,
        // though unlikely for timezone offsets.
        out_time.tz_offset_seconds = static_cast<int32_t>(offset_opt.value());
        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltTime& time, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x54;  // 'T'
            out_pss_sptr->fields.emplace_back(Value(time.nanoseconds_since_midnight));
            out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(time.tz_offset_seconds)));
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

    BoltError from_packstream(const PackStreamStructure& pss, BoltLocalTime& out_local_time) {
        if (pss.tag != 0x74) return BoltError::INVALID_MESSAGE_FORMAT;  // 't'
        if (pss.fields.size() != 1) return BoltError::INVALID_MESSAGE_FORMAT;

        auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        if (!nanos_opt.has_value()) return BoltError::INVALID_MESSAGE_FORMAT;

        out_local_time.nanoseconds_since_midnight = nanos_opt.value();
        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltLocalTime& local_time, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x74;  // 't'
            out_pss_sptr->fields.emplace_back(Value(local_time.nanoseconds_since_midnight));
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol