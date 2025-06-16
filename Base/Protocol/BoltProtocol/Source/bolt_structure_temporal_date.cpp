#include <variant>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/detail/bolt_structure_helpers.h"  // For get_typed_field

namespace boltprotocol {

    BoltError from_packstream(const PackStreamStructure& pss, BoltDate& out_date) {
        if (pss.tag != 0x44) return BoltError::INVALID_MESSAGE_FORMAT;  // 'D'
        if (pss.fields.size() != 1) return BoltError::INVALID_MESSAGE_FORMAT;

        auto days_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        if (!days_opt.has_value()) return BoltError::INVALID_MESSAGE_FORMAT;

        out_date.days_since_epoch = days_opt.value();
        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltDate& date, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x44;  // 'D'
            out_pss_sptr->fields.emplace_back(Value(date.days_since_epoch));
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol