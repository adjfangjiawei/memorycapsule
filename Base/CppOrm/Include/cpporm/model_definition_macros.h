#ifndef cpporm_MODEL_DEFINITION_MACROS_H
#define cpporm_MODEL_DEFINITION_MACROS_H

#include <any>
#include <initializer_list>
#include <map>
#include <memory>  // For std::shared_ptr
#include <sstream>
#include <string>
#include <typeindex>
#include <vector>

#include "cpporm/model_base.h"

// --- Helper Macros ---
#define cpporm_STRINGIFY_DETAIL(x) #x
#define cpporm_STRINGIFY(x) cpporm_STRINGIFY_DETAIL(x)
#define cpporm_CONCAT_DETAIL(x, y) x##y
#define cpporm_CONCAT(x, y) cpporm_CONCAT_DETAIL(x, y)

namespace cpporm {
    template <typename BaseFlagType, typename... Flags>
    constexpr BaseFlagType combine_flags_recursive(BaseFlagType base, Flags... flags) {
        if constexpr (sizeof...(flags) == 0) {
            return base;
        } else {
            return (base | ... | flags);
        }
    }
    namespace internal {
        template <size_t N_one_based, typename... Args>
        std::string get_optional_arg_str(const char *default_val_if_absent_or_empty, Args... args) {
            if constexpr (N_one_based > 0 && N_one_based <= sizeof...(Args)) {
                const char *arg_array[] = {args...};
                const char *selected_arg = arg_array[N_one_based - 1];
                if (selected_arg && selected_arg[0] != '\0') {
                    return selected_arg;
                }
            }
            return default_val_if_absent_or_empty ? default_val_if_absent_or_empty : "";
        }
    }  // namespace internal
    inline std::vector<std::string> _cpporm_make_string_vector(std::initializer_list<const char *> list) {
        std::vector<std::string> vec;
        vec.reserve(list.size());
        for (const char *s : list) {
            if (s && *s) {
                vec.emplace_back(s);
            }
        }
        return vec;
    }
}  // namespace cpporm

#define cpporm_DEFINE_MODEL_CLASS_NAME(ClassName) \
    using _cppormThisModelClass = ClassName;      \
    friend class cpporm::Model<ClassName>;

#define cpporm_MODEL_BEGIN(CurrentClassName, TableNameStr)                                                                                                      \
  private:                                                                                                                                                      \
    inline static const bool cpporm_CONCAT(_cpporm_tbl_init_, __COUNTER__) = (cpporm::Model<CurrentClassName>::_initSharedMetaTableName(TableNameStr), true);   \
    inline static const bool cpporm_CONCAT(_cpporm_mfinal_reg_, __COUNTER__) = (cpporm::internal::registerModelClassForFinalization<CurrentClassName>(), true); \
                                                                                                                                                                \
  public:

#undef cpporm_FIELD
#define cpporm_FIELD(CppType, CppName, DbNameStr, ...)                                                                                                                                                                                       \
  public:                                                                                                                                                                                                                                    \
    CppType CppName{};                                                                                                                                                                                                                       \
                                                                                                                                                                                                                                             \
  private:                                                                                                                                                                                                                                   \
    inline static const bool cpporm_CONCAT(_f_prov_reg_, CppName) = (cpporm::Model<_cppormThisModelClass>::_addPendingFieldMetaProvider([]() -> cpporm::FieldMeta {                                                                          \
                                                                         auto g = &cpporm::Model<_cppormThisModelClass>::template _cpporm_generated_getter<CppType, &_cppormThisModelClass::CppName>;                                        \
                                                                         auto s = &cpporm::Model<_cppormThisModelClass>::template _cpporm_generated_setter<CppType, &_cppormThisModelClass::CppName>;                                        \
                                                                         return cpporm::FieldMeta(DbNameStr, cpporm_STRINGIFY(CppName), typeid(CppType), "", cpporm::combine_flags_recursive(cpporm::FieldFlag::None, ##__VA_ARGS__), g, s); \
                                                                     }),                                                                                                                                                                     \
                                                                     true);                                                                                                                                                                  \
                                                                                                                                                                                                                                             \
  public:

#define cpporm_FIELD_TYPE(CppType, CppName, DbNameStr, DbTypeHintStr, ...)                                                                                                                                                                               \
  public:                                                                                                                                                                                                                                                \
    CppType CppName{};                                                                                                                                                                                                                                   \
                                                                                                                                                                                                                                                         \
  private:                                                                                                                                                                                                                                               \
    inline static const bool cpporm_CONCAT(_ft_prov_reg_, CppName) = (cpporm::Model<_cppormThisModelClass>::_addPendingFieldMetaProvider([]() -> cpporm::FieldMeta {                                                                                     \
                                                                          auto g = &cpporm::Model<_cppormThisModelClass>::template _cpporm_generated_getter<CppType, &_cppormThisModelClass::CppName>;                                                   \
                                                                          auto s = &cpporm::Model<_cppormThisModelClass>::template _cpporm_generated_setter<CppType, &_cppormThisModelClass::CppName>;                                                   \
                                                                          return cpporm::FieldMeta(DbNameStr, cpporm_STRINGIFY(CppName), typeid(CppType), DbTypeHintStr, cpporm::combine_flags_recursive(cpporm::FieldFlag::None, ##__VA_ARGS__), g, s); \
                                                                      }),                                                                                                                                                                                \
                                                                      true);                                                                                                                                                                             \
                                                                                                                                                                                                                                                         \
  public:

#undef cpporm_ASSOCIATION_FIELD
#define cpporm_ASSOCIATION_FIELD(ContainerCppType, CppName)                                                                                                                                                             \
  public:                                                                                                                                                                                                               \
    ContainerCppType CppName{};                                                                                                                                                                                         \
                                                                                                                                                                                                                        \
  private:                                                                                                                                                                                                              \
    inline static const bool cpporm_CONCAT(_assoc_f_prov_reg_, CppName) = (cpporm::Model<_cppormThisModelClass>::_addPendingFieldMetaProvider([]() -> cpporm::FieldMeta {                                               \
                                                                               return cpporm::FieldMeta("", cpporm_STRINGIFY(CppName), typeid(ContainerCppType), "", cpporm::FieldFlag::Association, nullptr, nullptr); \
                                                                           }),                                                                                                                                          \
                                                                           true);                                                                                                                                       \
                                                                                                                                                                                                                        \
  public:

// Association macros now pass a TargetTypeIndexProvider
#define cpporm_HAS_MANY(CppFieldName, AssocModelParamName, FKOnAssoc, ...)                                                                                                                                                                                                  \
  private:                                                                                                                                                                                                                                                                  \
    inline static const bool cpporm_CONCAT(_assoc_m_prov_reg_hm_, CppFieldName) = (cpporm::Model<_cppormThisModelClass>::_addPendingAssociationProvider([]() -> cpporm::AssociationMeta {                                                                                   \
                                                                                       auto d_setter_vec = &cpporm::Model<_cppormThisModelClass>::template _cpporm_generated_association_vector_setter<AssocModelParamName, &_cppormThisModelClass::CppFieldName>;          \
                                                                                       std::string current_model_ref_key = cpporm::internal::get_optional_arg_str<1>("", ##__VA_ARGS__);                                                                                    \
                                                                                       /* Create the provider function for target_model_type */                                                                                                                             \
                                                                                       cpporm::TargetTypeIndexProvider target_type_provider = &cpporm::Model<AssocModelParamName>::_get_static_type_index;                                                                  \
                                                                                       return cpporm::AssociationMeta(cpporm_STRINGIFY(CppFieldName), cpporm::AssociationType::HasMany, target_type_provider, FKOnAssoc, current_model_ref_key, "", d_setter_vec, nullptr); \
                                                                                   }),                                                                                                                                                                                      \
                                                                                   true);                                                                                                                                                                                   \
                                                                                                                                                                                                                                                                            \
  public:

#define cpporm_HAS_ONE(CppFieldName, AssocModelParamName, FKOnAssoc, ...)                                                                                                                                                                                                  \
  private:                                                                                                                                                                                                                                                                 \
    inline static const bool cpporm_CONCAT(_assoc_m_prov_reg_ho_, CppFieldName) = (cpporm::Model<_cppormThisModelClass>::_addPendingAssociationProvider([]() -> cpporm::AssociationMeta {                                                                                  \
                                                                                       auto d_setter_sgl = &cpporm::Model<_cppormThisModelClass>::template _cpporm_generated_association_single_setter<AssocModelParamName, &_cppormThisModelClass::CppFieldName>;         \
                                                                                       std::string current_model_ref_key = cpporm::internal::get_optional_arg_str<1>("", ##__VA_ARGS__);                                                                                   \
                                                                                       cpporm::TargetTypeIndexProvider target_type_provider = &cpporm::Model<AssocModelParamName>::_get_static_type_index;                                                                 \
                                                                                       return cpporm::AssociationMeta(cpporm_STRINGIFY(CppFieldName), cpporm::AssociationType::HasOne, target_type_provider, FKOnAssoc, current_model_ref_key, "", nullptr, d_setter_sgl); \
                                                                                   }),                                                                                                                                                                                     \
                                                                                   true);                                                                                                                                                                                  \
                                                                                                                                                                                                                                                                           \
  public:

#define cpporm_BELONGS_TO(CppFieldName, TargetModelParamName, FKOnCurrent, ...)                                                                                                                                                                                                         \
  private:                                                                                                                                                                                                                                                                              \
    inline static const bool cpporm_CONCAT(_assoc_m_prov_reg_bt_, CppFieldName) = (cpporm::Model<_cppormThisModelClass>::_addPendingAssociationProvider([]() -> cpporm::AssociationMeta {                                                                                               \
                                                                                       auto d_setter_sgl = &cpporm::Model<_cppormThisModelClass>::template _cpporm_generated_association_single_setter<TargetModelParamName, &_cppormThisModelClass::CppFieldName>;                     \
                                                                                       std::string target_model_ref_key = cpporm::internal::get_optional_arg_str<1>("", ##__VA_ARGS__);                                                                                                 \
                                                                                       cpporm::TargetTypeIndexProvider target_type_provider = &cpporm::Model<TargetModelParamName>::_get_static_type_index;                                                                             \
                                                                                       return cpporm::AssociationMeta(cpporm_STRINGIFY(CppFieldName), cpporm::AssociationType::BelongsTo, target_type_provider, FKOnCurrent, FKOnCurrent, target_model_ref_key, nullptr, d_setter_sgl); \
                                                                                   }),                                                                                                                                                                                                  \
                                                                                   true);                                                                                                                                                                                               \
                                                                                                                                                                                                                                                                                        \
  public:

#define cpporm_INDEX_INTERNAL(IsUniqueParam, IndexNameOrFirstColParam, ...)                                                                                                                                                                                                               \
  private:                                                                                                                                                                                                                                                                                \
    inline static const bool cpporm_CONCAT(_idx_def_prov_reg_, __COUNTER__) = (cpporm::Model<_cppormThisModelClass>::_addPendingIndexDefinitionProvider([]() -> cpporm::IndexDefinition {                                                                                                 \
                                                                                   cpporm::IndexDefinition def;                                                                                                                                                                           \
                                                                                   def.is_unique = IsUniqueParam;                                                                                                                                                                         \
                                                                                   const char *first_arg = IndexNameOrFirstColParam;                                                                                                                                                      \
                                                                                   std::initializer_list<const char *> other_cols_il = {__VA_ARGS__};                                                                                                                                     \
                                                                                   std::vector<std::string> other_cols_vec = cpporm::_cpporm_make_string_vector(other_cols_il);                                                                                                           \
                                                                                   if (!other_cols_vec.empty()) {                                                                                                                                                                         \
                                                                                       if (first_arg && *first_arg) def.index_name = first_arg;                                                                                                                                           \
                                                                                       def.db_column_names = other_cols_vec;                                                                                                                                                              \
                                                                                   } else if (first_arg && *first_arg) {                                                                                                                                                                  \
                                                                                       std::string temp_s(first_arg);                                                                                                                                                                     \
                                                                                       std::stringstream ss_cols(temp_s);                                                                                                                                                                 \
                                                                                       std::string segment;                                                                                                                                                                               \
                                                                                       bool looks_like_single_col_name_not_index_name = (temp_s.find(',') == std::string::npos && temp_s.find(' ') == std::string::npos && temp_s.rfind("idx_", 0) != 0 && temp_s.rfind("uix_", 0) != 0); \
                                                                                       std::vector<std::string> parsed_cols;                                                                                                                                                              \
                                                                                       while (std::getline(ss_cols, segment, ',')) {                                                                                                                                                      \
                                                                                           size_t f = segment.find_first_not_of(" \t");                                                                                                                                                   \
                                                                                           if (f != std::string::npos) {                                                                                                                                                                  \
                                                                                               size_t l = segment.find_last_not_of(" \t");                                                                                                                                                \
                                                                                               parsed_cols.push_back(segment.substr(f, (l - f + 1)));                                                                                                                                     \
                                                                                           }                                                                                                                                                                                              \
                                                                                       }                                                                                                                                                                                                  \
                                                                                       if (parsed_cols.size() > 1 || looks_like_single_col_name_not_index_name) {                                                                                                                         \
                                                                                           def.db_column_names = parsed_cols;                                                                                                                                                             \
                                                                                       } else {                                                                                                                                                                                           \
                                                                                           def.index_name = temp_s;                                                                                                                                                                       \
                                                                                           if (def.db_column_names.empty()) {                                                                                                                                                             \
                                                                                           }                                                                                                                                                                                              \
                                                                                       }                                                                                                                                                                                                  \
                                                                                   }                                                                                                                                                                                                      \
                                                                                   if (def.db_column_names.empty() && !def.index_name.empty()) {                                                                                                                                          \
                                                                                   } else if (def.db_column_names.empty() && def.index_name.empty()) {                                                                                                                                    \
                                                                                   }                                                                                                                                                                                                      \
                                                                                   return def;                                                                                                                                                                                            \
                                                                               }),                                                                                                                                                                                                        \
                                                                               true);                                                                                                                                                                                                     \
                                                                                                                                                                                                                                                                                          \
  public:

#define cpporm_INDEX(IndexNameOrFirstCol, ...) cpporm_INDEX_INTERNAL(false, IndexNameOrFirstCol, ##__VA_ARGS__)
#define cpporm_UNIQUE_INDEX(IndexNameOrFirstCol, ...) cpporm_INDEX_INTERNAL(true, IndexNameOrFirstCol, ##__VA_ARGS__)

#undef cpporm_PRIMARY_KEY
#define cpporm_PRIMARY_KEY(CppType, CppName, DbNameStr, ...)                                                                                                                                                                                                                    \
  public:                                                                                                                                                                                                                                                                       \
    CppType CppName{};                                                                                                                                                                                                                                                          \
                                                                                                                                                                                                                                                                                \
  private:                                                                                                                                                                                                                                                                      \
    inline static const bool cpporm_CONCAT(_pk_prov_reg_, CppName) = (cpporm::Model<_cppormThisModelClass>::_addPendingFieldMetaProvider([]() -> cpporm::FieldMeta {                                                                                                            \
                                                                          auto g = &cpporm::Model<_cppormThisModelClass>::template _cpporm_generated_getter<CppType, &_cppormThisModelClass::CppName>;                                                                          \
                                                                          auto s = &cpporm::Model<_cppormThisModelClass>::template _cpporm_generated_setter<CppType, &_cppormThisModelClass::CppName>;                                                                          \
                                                                          return cpporm::FieldMeta(DbNameStr, cpporm_STRINGIFY(CppName), typeid(CppType), "", cpporm::combine_flags_recursive(cpporm::FieldFlag::PrimaryKey, cpporm::FieldFlag::NotNull, ##__VA_ARGS__), g, s); \
                                                                      }),                                                                                                                                                                                                       \
                                                                      true);                                                                                                                                                                                                    \
                                                                                                                                                                                                                                                                                \
  public:

#define cpporm_AUTO_INCREMENT_PRIMARY_KEY(CppType, CppName, DbNameStr) cpporm_PRIMARY_KEY(CppType, CppName, DbNameStr, cpporm::FieldFlag::AutoIncrement)

#define cpporm_TIMESTAMPS(TimestampCppType) cpporm_FIELD_TYPE(TimestampCppType, created_at, "created_at", "DATETIME", cpporm::FieldFlag::CreatedAt) cpporm_FIELD_TYPE(TimestampCppType, updated_at, "updated_at", "DATETIME", cpporm::FieldFlag::UpdatedAt)

#define cpporm_SOFT_DELETE(TimestampCppType) cpporm_FIELD_TYPE(TimestampCppType, deleted_at, "deleted_at", "DATETIME", cpporm::FieldFlag::DeletedAt, cpporm::FieldFlag::HasDefault)

#define cpporm_MODEL_END()

#endif  // cpporm_MODEL_DEFINITION_MACROS_H