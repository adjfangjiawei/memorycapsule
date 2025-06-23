#ifndef cpporm_MODEL_META_H
#define cpporm_MODEL_META_H

#include <algorithm>
#include <string>
#include <vector>

#include "cpporm/model_meta_definitions.h"

namespace cpporm {

    // --- ModelMeta Definition ---
    struct ModelMeta {
        std::string table_name;
        std::vector<FieldMeta> fields;
        std::vector<AssociationMeta> associations;
        std::vector<std::string> primary_keys_db_names;
        std::vector<IndexDefinition> indexes;
        bool _is_finalized = false;

        const FieldMeta *findFieldByDbName(const std::string &name) const {
            for (const auto &f : fields)
                if (f.db_name == name && !f.db_name.empty()) return &f;
            return nullptr;
        }
        const FieldMeta *findFieldByCppName(const std::string &name) const {
            for (const auto &f : fields)
                if (f.cpp_name == name) return &f;
            return nullptr;
        }
        const AssociationMeta *findAssociationByCppName(const std::string &cpp_assoc_field_name) const {
            for (const auto &assoc : associations)
                if (assoc.cpp_field_name == cpp_assoc_field_name) return &assoc;
            return nullptr;
        }
        const FieldMeta *getPrimaryField(size_t idx = 0) const {
            if (primary_keys_db_names.empty() || idx >= primary_keys_db_names.size()) return nullptr;
            return findFieldByDbName(primary_keys_db_names[idx]);
        }
        std::vector<const FieldMeta *> getPrimaryKeyFields() const {
            std::vector<const FieldMeta *> pks;
            pks.reserve(primary_keys_db_names.size());
            for (const auto &pk_name : primary_keys_db_names) {
                if (auto *f = findFieldByDbName(pk_name)) pks.push_back(f);
            }
            return pks;
        }
        const FieldMeta *findFieldWithFlag(FieldFlag flag_to_find) const {
            auto it = std::find_if(fields.begin(), fields.end(), [flag_to_find](const FieldMeta &fm) {
                return has_flag(fm.flags, flag_to_find);
            });
            return (it == fields.end()) ? nullptr : &(*it);
        }
    };

}  // namespace cpporm

#endif  // cpporm_MODEL_META_H