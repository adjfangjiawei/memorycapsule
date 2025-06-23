#ifndef cpporm_MODEL_META_DEFINITIONS_H
#define cpporm_MODEL_META_DEFINITIONS_H

#include <any>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include "cpporm/model_types.h"

namespace cpporm {

    // Forward declarations
    class ModelBase;

    // --- Index Definition ---
    struct IndexDefinition {
        std::string index_name;
        std::vector<std::string> db_column_names;
        bool is_unique = false;
        std::string type_str;
        std::string condition_str;
    };

    // Type-erased provider for target model's type_index
    using TargetTypeIndexProvider = std::function<std::type_index()>;

    struct AssociationMeta {
        std::string cpp_field_name;
        AssociationType type = AssociationType::None;

        TargetTypeIndexProvider target_type_index_provider;
        std::type_index target_model_type;  // To be filled during finalization

        std::string foreign_key_db_name;
        std::string primary_key_db_name_on_current_model;
        std::string target_model_pk_db_name;

        std::function<void(void * /* model_instance */, std::vector<std::shared_ptr<ModelBase>> & /* associated_models */)> data_setter_vector;

        std::function<void(void * /* model_instance */, std::shared_ptr<ModelBase> /* associated_model */)> data_setter_single;

        AssociationMeta(std::string fieldName,
                        AssociationType assocType,
                        TargetTypeIndexProvider targetTypeProvider,
                        std::string fkDbName,
                        std::string currentModelRefKeyDbName = "",
                        std::string targetModelReferencedKeyDbName = "",
                        std::function<void(void *, std::vector<std::shared_ptr<ModelBase>> &)> vec_setter = nullptr,
                        std::function<void(void *, std::shared_ptr<ModelBase>)> single_setter = nullptr)
            : cpp_field_name(std::move(fieldName)),
              type(assocType),
              target_type_index_provider(std::move(targetTypeProvider)),
              target_model_type(typeid(void)),
              foreign_key_db_name(std::move(fkDbName)),
              primary_key_db_name_on_current_model(std::move(currentModelRefKeyDbName)),
              target_model_pk_db_name(std::move(targetModelReferencedKeyDbName)),
              data_setter_vector(std::move(vec_setter)),
              data_setter_single(std::move(single_setter)) {
        }
    };

    // --- Field Metadata ---
    struct FieldMeta {
        std::string db_name;
        std::string cpp_name;
        std::type_index cpp_type;
        std::string db_type_hint;
        std::string comment;  // ***** 新增 comment 成员 *****
        FieldFlag flags = FieldFlag::None;
        std::any default_value;

        std::function<std::any(const void *)> getter;
        std::function<void(void *, const std::any &)> setter;

        FieldMeta(std::string dbName,
                  std::string cppName,
                  std::type_index cppType,
                  std::string dbTypeHint = "",
                  std::string fieldComment = "",  // ***** 新增构造函数参数 *****
                  FieldFlag fieldFlags = FieldFlag::None,
                  std::function<std::any(const void *)> g = nullptr,
                  std::function<void(void *, const std::any &)> s = nullptr)
            : db_name(std::move(dbName)),
              cpp_name(std::move(cppName)),
              cpp_type(cppType),
              db_type_hint(std::move(dbTypeHint)),
              comment(std::move(fieldComment)),  // ***** 初始化 comment 成员 *****
              flags(fieldFlags),
              getter(std::move(g)),
              setter(std::move(s)) {
        }
    };

}  // namespace cpporm

#endif  // cpporm_MODEL_META_DEFINITIONS_H