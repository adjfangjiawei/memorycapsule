#include <variant>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/detail/bolt_structure_helpers.h"

namespace boltprotocol {

    BoltError from_packstream(const PackStreamStructure& pss, BoltDuration& out_duration) {
        if (pss.tag != 0x45) return BoltError::INVALID_MESSAGE_FORMAT;  // 'E'
        if (pss.fields.size() != 4) return BoltError::INVALID_MESSAGE_FORMAT;

        auto months_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        auto days_opt = detail::get_typed_field<int64_t>(pss.fields, 1);
        auto seconds_opt = detail::get_typed_field<int64_t>(pss.fields, 2);
        auto nanos_opt = detail::get_typed_field<int64_t>(pss.fields, 3);  // Stored as int64_t in Value

        if (!months_opt || !days_opt || !seconds_opt || !nanos_opt) return BoltError::INVALID_MESSAGE_FORMAT;

        out_duration.months = months_opt.value();
        out_duration.days = days_opt.value();
        out_duration.seconds = seconds_opt.value();
        out_duration.nanoseconds = static_cast<int32_t>(nanos_opt.value());
        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltDuration& duration, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x45;  // 'E'
            out_pss_sptr->fields.emplace_back(Value(duration.months));
            out_pss_sptr->fields.emplace_back(Value(duration.days));
            out_pss_sptr->fields.emplace_back(Value(duration.seconds));
            out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(duration.nanoseconds)));
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol