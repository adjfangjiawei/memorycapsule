#include <variant>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/detail/bolt_structure_helpers.h"

namespace boltprotocol {

    BoltError from_packstream(const PackStreamStructure& pss, BoltPoint2D& out_point) {
        if (pss.tag != 0x58) return BoltError::INVALID_MESSAGE_FORMAT;  // 'X'
        if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;

        auto srid_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        auto x_opt = detail::get_typed_field<double>(pss.fields, 1);
        auto y_opt = detail::get_typed_field<double>(pss.fields, 2);

        if (!srid_opt || !x_opt || !y_opt) return BoltError::INVALID_MESSAGE_FORMAT;

        out_point.srid = static_cast<uint32_t>(srid_opt.value());
        out_point.x = x_opt.value();
        out_point.y = y_opt.value();
        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltPoint2D& point, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x58;  // 'X'
            out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(point.srid)));
            out_pss_sptr->fields.emplace_back(Value(point.x));
            out_pss_sptr->fields.emplace_back(Value(point.y));
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

    BoltError from_packstream(const PackStreamStructure& pss, BoltPoint3D& out_point) {
        if (pss.tag != 0x59) return BoltError::INVALID_MESSAGE_FORMAT;  // 'Y'
        if (pss.fields.size() != 4) return BoltError::INVALID_MESSAGE_FORMAT;

        auto srid_opt = detail::get_typed_field<int64_t>(pss.fields, 0);
        auto x_opt = detail::get_typed_field<double>(pss.fields, 1);
        auto y_opt = detail::get_typed_field<double>(pss.fields, 2);
        auto z_opt = detail::get_typed_field<double>(pss.fields, 3);

        if (!srid_opt || !x_opt || !y_opt || !z_opt) return BoltError::INVALID_MESSAGE_FORMAT;

        out_point.srid = static_cast<uint32_t>(srid_opt.value());
        out_point.x = x_opt.value();
        out_point.y = y_opt.value();
        out_point.z = z_opt.value();
        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltPoint3D& point, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x59;  // 'Y'
            out_pss_sptr->fields.emplace_back(Value(static_cast<int64_t>(point.srid)));
            out_pss_sptr->fields.emplace_back(Value(point.x));
            out_pss_sptr->fields.emplace_back(Value(point.y));
            out_pss_sptr->fields.emplace_back(Value(point.z));
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol