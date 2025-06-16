#include <variant>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/detail/bolt_structure_helpers.h"

namespace boltprotocol {

    BoltError from_packstream(const PackStreamStructure& pss, BoltPath& out_path, const versions::Version& bolt_version) {
        if (pss.tag != 0x50) return BoltError::INVALID_MESSAGE_FORMAT;
        if (pss.fields.size() != 3) return BoltError::INVALID_MESSAGE_FORMAT;

        auto nodes_vec_opt = detail::get_typed_list_field<BoltNode>(pss.fields, 0, &bolt_version);
        if (!nodes_vec_opt) return BoltError::INVALID_MESSAGE_FORMAT;
        try {
            out_path.nodes = std::move(nodes_vec_opt.value());
        } catch (...) {
            return BoltError::OUT_OF_MEMORY;
        }

        auto rels_vec_opt = detail::get_typed_list_field<BoltUnboundRelationship>(pss.fields, 1, &bolt_version);
        if (!rels_vec_opt) return BoltError::INVALID_MESSAGE_FORMAT;
        try {
            out_path.rels = std::move(rels_vec_opt.value());
        } catch (...) {
            return BoltError::OUT_OF_MEMORY;
        }

        auto indices_list_sptr_opt = detail::get_typed_field<std::shared_ptr<BoltList>>(pss.fields, 2);
        if (!indices_list_sptr_opt || !indices_list_sptr_opt.value()) return BoltError::INVALID_MESSAGE_FORMAT;

        const auto& indices_list_sptr = indices_list_sptr_opt.value();
        out_path.indices.clear();
        try {
            out_path.indices.reserve(indices_list_sptr->elements.size());
            for (const auto& idx_val : indices_list_sptr->elements) {
                if (std::holds_alternative<int64_t>(idx_val)) {
                    out_path.indices.push_back(std::get<int64_t>(idx_val));
                } else {
                    return BoltError::INVALID_MESSAGE_FORMAT;
                }
            }
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::UNKNOWN_ERROR;
        }

        return BoltError::SUCCESS;
    }

    BoltError to_packstream(const BoltPath& path, const versions::Version& bolt_version, std::shared_ptr<PackStreamStructure>& out_pss_sptr) {
        try {
            out_pss_sptr = std::make_shared<PackStreamStructure>();
            out_pss_sptr->tag = 0x50;

            auto nodes_list_sptr = std::make_shared<BoltList>();
            nodes_list_sptr->elements.reserve(path.nodes.size());
            for (const auto& node : path.nodes) {
                std::shared_ptr<PackStreamStructure> node_pss_sptr;
                BoltError err = to_packstream(node, bolt_version, node_pss_sptr);
                if (err != BoltError::SUCCESS) return err;
                nodes_list_sptr->elements.emplace_back(Value(node_pss_sptr));
            }
            out_pss_sptr->fields.emplace_back(Value(nodes_list_sptr));

            auto rels_list_sptr = std::make_shared<BoltList>();
            rels_list_sptr->elements.reserve(path.rels.size());
            for (const auto& rel : path.rels) {
                std::shared_ptr<PackStreamStructure> rel_pss_sptr;
                BoltError err = to_packstream(rel, bolt_version, rel_pss_sptr);
                if (err != BoltError::SUCCESS) return err;
                rels_list_sptr->elements.emplace_back(Value(rel_pss_sptr));
            }
            out_pss_sptr->fields.emplace_back(Value(rels_list_sptr));

            auto indices_list_sptr = std::make_shared<BoltList>();
            indices_list_sptr->elements.reserve(path.indices.size());
            for (const auto& idx : path.indices) {
                indices_list_sptr->elements.emplace_back(Value(idx));
            }
            out_pss_sptr->fields.emplace_back(Value(indices_list_sptr));

        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            return BoltError::SERIALIZATION_ERROR;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol