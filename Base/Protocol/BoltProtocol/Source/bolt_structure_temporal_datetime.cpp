#include <variant>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/detail/bolt_structure_helpers.h"

namespace boltprotocol {

    // DateTime (Modern 'I' and Legacy 'F')
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTime& out_datetime, const versions::Version& bolt_version) {
        // This function handles both modern (tag 'I') and legacy (tag 'F') DateTime.
        // The caller should ideally pass a PackStreamStructure that was already identified
        // as one of these. The `bolt_version` helps decide which one to primarily expect or how to interpret.

        if (pss.tag == 0x49) {  // Modern DateTime 'I'
            if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;
            auto secs_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
            auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);   // Stored as int64_t in Value
            auto offset_opt = detail::get_typed_field<int64_t>(pss.fields, 2);  // Stored as int64_t in Value

            if (!secs_opt || !nanos_opt || !offset_opt) return BoltError::INVALID_MESSAGE_FORMAT;

            out_datetime.seconds_epoch_utc = secs_opt.value();
            out_datetime.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
            out_datetime.tz_offset_seconds = static_cast<int32_t>(offset_opt.value());
            return BoltError::SUCCESS;

        } else if (pss.tag == 0x46) {  // Legacy DateTime 'F'
            if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;
            auto secs_adjusted_opt = detail::get_typed_field<int64_t>(pss.fields, 0);  // seconds from epoch + offset
            auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
            auto offset_opt = detail::get_typed_field<int64_t>(pss.fields, 2);

            if (!secs_adjusted_opt || !nanos_opt || !offset_opt) return BoltError::INVALID_MESSAGE_FORMAT;

            out_datetime.tz_offset_seconds = static_cast<int32_t>(offset_opt.value());
            out_datetime.seconds_epoch_utc = secs_adjusted_opt.value() - out_datetime.tz_offset_seconds;
            out_datetime.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
            return BoltError::SUCCESS;
        }
        return BoltError::INVALID_MESSAGE_FORMAT;  // Unknown tag for DateTime
    }

    BoltError to_packstream(const BoltDateTime& datetime, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        // Determine if "utc" patch is active or if version implies modern structures.
        // This simple check assumes modern for 5.0+ or 4.4 with a conceptual "utc_patch_active" flag.
        // A real driver would get this patch status from HELLO negotiation.
        bool use_modern_datetime = (bolt_version.major >= 5) || (bolt_version.major == 4 && bolt_version.minor == 4 /* && utc_patch_active */);
        // If not explicitly modern, default to legacy for pre-5.0/pre-patched 4.4.

        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            if (use_modern_datetime) {
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

    // DateTimeZoneId (Modern 'i' and Legacy 'f')
    BoltError from_packstream(const PackStreamStructure& pss, BoltDateTimeZoneId& out_datetime_zoneid, const versions::Version& bolt_version) {
        if (pss.tag == 0x69) {  // Modern 'i'
            if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;
            auto secs_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
            auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
            auto tzid_opt = detail::get_typed_field<std::string>(pss.fields, 2);

            if (!secs_opt || !nanos_opt || !tzid_opt) return BoltError::INVALID_MESSAGE_FORMAT;

            out_datetime_zoneid.seconds_epoch_utc = secs_opt.value();
            out_datetime_zoneid.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
            out_datetime_zoneid.tz_id = tzid_opt.value();
            return BoltError::SUCCESS;

        } else if (pss.tag == 0x66) {  // Legacy 'f'
            // According to spec, legacy DateTimeZoneId also has 3 fields.
            // The interpretation of 'seconds' field is different.
            // seconds = epoch_seconds_utc + offset_derived_from_tz_id_at_that_instant
            // This makes deserialization tricky as one needs to resolve tz_id to an offset.
            // For simplification, if a library doesn't do full timezone math, it might treat
            // the 'seconds' field as an already adjusted local time and store tz_id.
            // A full implementation would need a timezone database.
            // Here, we'll assume the user of this struct will handle the tz_id correctly
            // if they get a legacy structure.
            if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;
            auto secs_adjusted_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
            auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
            auto tzid_opt = detail::get_typed_field<std::string>(pss.fields, 2);

            if (!secs_adjusted_opt || !nanos_opt || !tzid_opt) return BoltError::INVALID_MESSAGE_FORMAT;

            // Storing as if it were modern UTC seconds for now.
            // A truly correct legacy deserialization would need to reverse the offset addition.
            // This is a known complexity with legacy DateTimeZoneId.
            out_datetime_zoneid.seconds_epoch_utc = secs_adjusted_opt.value();  // This is NOT UTC for legacy!
            out_datetime_zoneid.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
            out_datetime_zoneid.tz_id = tzid_opt.value();
            // User must be aware if (pss.tag == 0x66) that seconds_epoch_utc is actually local-like.
            return BoltError::SUCCESS;
        }
        return BoltError::INVALID_MESSAGE_FORMAT;
    }

    BoltError to_packstream(const BoltDateTimeZoneId& datetime_zoneid, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        bool use_modern_datetime = (bolt_version.major >= 5) || (bolt_version.major == 4 && bolt_version.minor == 4 /* && utc_patch_active */);
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            if (use_modern_datetime) {
                out_pss_sptr->tag = 0x69;  // 'i'
                out_pss_sptr->fields.emplace_back(Value(datetime_zoneid.seconds_epoch_utc));
                out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(datetime_zoneid.nanoseconds_of_second)));
                out_pss_sptr->fields.emplace_back(Value(datetime_zoneid.tz_id));
            } else {                       // Use Legacy DateTimeZoneId 'f'
                out_pss_sptr->tag = 0x66;  // 'f'
                // For legacy serialization, 'seconds' = epoch_seconds_utc + offset_for_tzid_at_instant
                // This requires resolving tz_id to an offset, which this library doesn't do.
                // Sending UTC seconds as the first field for legacy is incorrect.
                // This highlights the difficulty of fully supporting legacy types without a TZDB.
                // As a placeholder, we'll serialize modern-like values, which will be wrong for legacy.
                // A consuming legacy server would misinterpret this.
                // A proper solution would be to disallow serializing to legacy format from this struct,
                // or require the offset to be pre-calculated and provided.
                // For now, let's make it an error to serialize modern BoltDateTimeZoneId to legacy format
                // unless the user explicitly handles the 'seconds' field adjustment.
                // Returning an error if trying to serialize to legacy without more info.
                // If seconds_epoch_utc is already pre-adjusted by caller for legacy, then it's fine.
                // Let's assume seconds_epoch_utc is always UTC.
                return BoltError::SERIALIZATION_ERROR;  // Cannot correctly serialize modern struct to legacy format without offset calc.
            }
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

    // LocalDateTime
    BoltError from_packstream(const PackStreamStructure& pss, BoltLocalDateTime& out_local_datetime) {
        if (pss.tag != 0x64) return BoltError::INVALID_MESSAGE_FORMAT;  // 'd'
        if (pss.fields.size() != 2) return BoltError::INVALID_MESSAGE_FORMAT;
        auto secs_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
        if (!secs_opt || !nanos_opt) return BoltError::INVALID_MESSAGE_FORMAT;
        out_local_datetime.seconds_epoch_local = secs_opt.value();
        out_local_datetime.nanoseconds_of_second = static_cast<int32_t>(nanos_opt.value());
        return BoltError::SUCCESS;
    }
    BoltError to_packstream(const BoltLocalDateTime& local_datetime, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x64;  // 'd'
            out_pss_sptr->fields.emplace_back(Value(local_datetime.seconds_epoch_local));
            out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(local_datetime.nanoseconds_of_second)));
        } catch (...) {
            return BoltError::OUT_OF_MEMORY;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol