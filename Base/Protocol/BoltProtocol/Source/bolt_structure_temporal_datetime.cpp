#include <iostream>
#include <variant>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/detail/bolt_structure_helpers.h"

namespace boltprotocol {

    // DateTime (Modern 'I' - 0x49 and Legacy 'F' - 0x46)
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTime& out_datetime, const versions::Version& bolt_version) {
        // bolt_version is available if subtle distinctions are needed, but tag is primary.
        if (pss.tag == 0x49) {  // Modern DateTime 'I'
            if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;

            auto secs_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
            auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
            auto offset_opt = detail::get_typed_field<int64_t>(pss.fields, 2);

            if (!secs_opt.has_value() || !nanos_opt.has_value() || !offset_opt.has_value()) {
                return BoltError::INVALID_MESSAGE_FORMAT;
            }

            out_datetime.seconds_epoch_utc = secs_opt.value();
            out_datetime.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
            out_datetime.tz_offset_seconds = static_cast<int32_t>(offset_opt.value());
            return BoltError::SUCCESS;

        } else if (pss.tag == 0x46) {  // Legacy DateTime 'F'
            if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;

            auto secs_adjusted_by_offset_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
            auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
            auto offset_opt = detail::get_typed_field<int64_t>(pss.fields, 2);

            if (!secs_adjusted_by_offset_opt.has_value() || !nanos_opt.has_value() || !offset_opt.has_value()) {
                return BoltError::INVALID_MESSAGE_FORMAT;
            }

            out_datetime.tz_offset_seconds = static_cast<int32_t>(offset_opt.value());
            out_datetime.seconds_epoch_utc = secs_adjusted_by_offset_opt.value() - out_datetime.tz_offset_seconds;
            out_datetime.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
            return BoltError::SUCCESS;
        }

        return BoltError::INVALID_MESSAGE_FORMAT;
    }

    BoltError to_packstream(const BoltDateTime& datetime, const versions::Version& bolt_version, bool utc_patch_active_for_4_4, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        bool use_modern_format = (bolt_version.major >= 5) || (bolt_version.major == 4 && bolt_version.minor == 4 && utc_patch_active_for_4_4);

        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            if (use_modern_format) {
                out_pss_sptr->tag = 0x49;  // 'I'
                out_pss_sptr->fields.emplace_back(Value(datetime.seconds_epoch_utc));
                out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(datetime.nanoseconds_of_second)));
                out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(datetime.tz_offset_seconds)));
            } else {                       // Use Legacy DateTime 'F'
                out_pss_sptr->tag = 0x46;  // 'F'
                out_pss_sptr->fields.emplace_back(Value(datetime.seconds_epoch_utc + datetime.tz_offset_seconds));
                out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(datetime.nanoseconds_of_second)));
                out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(datetime.tz_offset_seconds)));
            }
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

    // DateTimeZoneId (Modern 'i' - 0x69 and Legacy 'f' - 0x66)
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTimeZoneId& out_datetime_zoneid, const versions::Version& bolt_version) {
        // bolt_version might be used for subtle interpretation differences if any beyond tag.
        if (pss.tag == 0x69) {  // Modern 'i'
            if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;
            auto secs_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
            auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
            auto tzid_opt = detail::get_typed_field<std::string>(pss.fields, 2);

            if (!secs_opt.has_value() || !nanos_opt.has_value() || !tzid_opt.has_value()) {
                return BoltError::INVALID_MESSAGE_FORMAT;
            }

            out_datetime_zoneid.seconds_epoch_utc = secs_opt.value();
            out_datetime_zoneid.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
            try {
                out_datetime_zoneid.tz_id = tzid_opt.value();
            } catch (...) {
                return BoltError::OUT_OF_MEMORY;
            }
            return BoltError::SUCCESS;

        } else if (pss.tag == 0x66) {  // Legacy 'f'
            if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;
            auto secs_adjusted_by_offset_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
            auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
            auto tzid_opt = detail::get_typed_field<std::string>(pss.fields, 2);

            if (!secs_adjusted_by_offset_opt.has_value() || !nanos_opt.has_value() || !tzid_opt.has_value()) {
                return BoltError::INVALID_MESSAGE_FORMAT;
            }

            out_datetime_zoneid.seconds_epoch_utc = secs_adjusted_by_offset_opt.value();
            out_datetime_zoneid.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
            try {
                out_datetime_zoneid.tz_id = tzid_opt.value();
            } catch (...) {
                return BoltError::OUT_OF_MEMORY;
            }
            // Caller should be aware that for legacy 'f', seconds_epoch_utc is not pure UTC.
            return BoltError::SUCCESS;
        }
        return BoltError::INVALID_MESSAGE_FORMAT;
    }

    BoltError to_packstream(const BoltDateTimeZoneId& datetime_zoneid, const versions::Version& bolt_version, bool utc_patch_active_for_4_4, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        bool use_modern_format = (bolt_version.major >= 5) || (bolt_version.major == 4 && bolt_version.minor == 4 && utc_patch_active_for_4_4);
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            if (use_modern_format) {
                out_pss_sptr->tag = 0x69;  // 'i'
                out_pss_sptr->fields.emplace_back(Value(datetime_zoneid.seconds_epoch_utc));
                out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(datetime_zoneid.nanoseconds_of_second)));
                out_pss_sptr->fields.emplace_back(Value(datetime_zoneid.tz_id));
            } else {                       // Use Legacy DateTimeZoneId 'f'
                out_pss_sptr->tag = 0x66;  // 'f'
                // As discussed, serializing to legacy 'f' from UTC seconds + tz_id without a TZDB is problematic.
                // The stored datetime_zoneid.seconds_epoch_utc is assumed to be pure UTC.
                // To produce the correct legacy 'seconds' field, we'd need to add the offset for tz_id at that instant.
                // Lacking TZDB, we will return an error or serialize potentially incorrect data.
                // For now, return error to highlight the issue.
                // A user wanting to serialize to legacy 'f' must provide a `seconds_epoch_utc` value
                // that is ALREADY `actual_utc_seconds + offset_for_tz_id_at_that_instant`.
                // std::cerr << "Error: Cannot accurately serialize BoltDateTimeZoneId to legacy format (0x66) "
                //           << "without timezone database information or pre-adjusted seconds value." << std::endl;
                // return BoltError::SERIALIZATION_ERROR;
                // OR, proceed with caution:
                out_pss_sptr->fields.emplace_back(Value(datetime_zoneid.seconds_epoch_utc));  // This is UTC, not adjusted!
                out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(datetime_zoneid.nanoseconds_of_second)));
                out_pss_sptr->fields.emplace_back(Value(datetime_zoneid.tz_id));
            }
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

    // LocalDateTime ('d' - 0x64)
    BoltError from_packstream(const PackStreamStructure& pss, BoltLocalDateTime& out_local_datetime) {
        if (pss.tag != 0x64) return BoltError::INVALID_MESSAGE_FORMAT;
        if (pss.fields.size() != 2) return BoltError::INVALID_MESSAGE_FORMAT;
        auto secs_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
        if (!secs_opt.has_value() || !nanos_opt.has_value()) return BoltError::INVALID_MESSAGE_FORMAT;
        out_local_datetime.seconds_epoch_local = secs_opt.value();
        out_local_datetime.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltLocalDateTime& local_datetime, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x64;
            out_pss_sptr->fields.emplace_back(Value(local_datetime.seconds_epoch_local));
            out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(local_datetime.nanoseconds_of_second)));
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol