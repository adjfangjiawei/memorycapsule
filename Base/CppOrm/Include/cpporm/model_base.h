// cpporm/model_base.h
#ifndef cpporm_MODEL_BASE_H
#define cpporm_MODEL_BASE_H

#include "cpporm/error.h"
#include <QDebug>
#include <algorithm>
#include <any>
#include <cstdint>
#include <functional>
#include <map>
#include <memory> // For std::shared_ptr
#include <mutex>
#include <string>
#include <typeindex>
#include <vector>

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QTime>

namespace cpporm {

// Forward declarations
class Session;
class QueryBuilder;
class ModelBase;
struct ModelMeta;
struct FieldMeta;
struct AssociationMeta;
struct IndexDefinition;

// --- Index Definition ---
struct IndexDefinition {
  std::string index_name;
  std::vector<std::string> db_column_names;
  bool is_unique = false;
  std::string type_str;
  std::string condition_str;
};

// --- Association Related Enums and Structs ---
enum class AssociationType { None, HasOne, BelongsTo, HasMany, ManyToMany };

// Type-erased provider for target model's type_index
using TargetTypeIndexProvider = std::function<std::type_index()>;

struct AssociationMeta {
  std::string cpp_field_name;
  AssociationType type = AssociationType::None;

  // Store the provider function instead of the type_index directly at
  // registration
  TargetTypeIndexProvider target_type_index_provider;
  std::type_index target_model_type; // To be filled during finalization

  std::string foreign_key_db_name;
  std::string primary_key_db_name_on_current_model;
  std::string target_model_pk_db_name;

  std::function<void(
      void * /* model_instance */,
      std::vector<std::shared_ptr<ModelBase>> & /* associated_models */)>
      data_setter_vector;

  std::function<void(void * /* model_instance */,
                     std::shared_ptr<ModelBase> /* associated_model */)>
      data_setter_single;

  AssociationMeta(
      std::string fieldName, AssociationType assocType,
      TargetTypeIndexProvider targetTypeProvider, // Changed parameter
      std::string fkDbName, std::string currentModelRefKeyDbName = "",
      std::string targetModelReferencedKeyDbName = "",
      std::function<void(void *, std::vector<std::shared_ptr<ModelBase>> &)>
          vec_setter = nullptr,
      std::function<void(void *, std::shared_ptr<ModelBase>)> single_setter =
          nullptr)
      : cpp_field_name(std::move(fieldName)), type(assocType),
        target_type_index_provider(std::move(targetTypeProvider)),
        target_model_type(typeid(void)), // Initialize to a dummy value
        foreign_key_db_name(std::move(fkDbName)),
        primary_key_db_name_on_current_model(
            std::move(currentModelRefKeyDbName)),
        target_model_pk_db_name(std::move(targetModelReferencedKeyDbName)),
        data_setter_vector(std::move(vec_setter)),
        data_setter_single(std::move(single_setter)) {}
};

// --- Field Flags ---
enum class FieldFlag : uint32_t {
  None = 0,
  PrimaryKey = 1 << 0,
  AutoIncrement = 1 << 1,
  NotNull = 1 << 2,
  Unique = 1 << 3,
  HasDefault = 1 << 4,
  Indexed = 1 << 5,
  CreatedAt = 1 << 6,
  UpdatedAt = 1 << 7,
  DeletedAt = 1 << 8,
  Association = 1 << 9
};

inline FieldFlag operator|(FieldFlag a, FieldFlag b) {
  return static_cast<FieldFlag>(static_cast<uint32_t>(a) |
                                static_cast<uint32_t>(b));
}
inline FieldFlag operator&(FieldFlag a, FieldFlag b) {
  return static_cast<FieldFlag>(static_cast<uint32_t>(a) &
                                static_cast<uint32_t>(b));
}
inline FieldFlag &operator|=(FieldFlag &a, FieldFlag b) {
  a = a | b;
  return a;
}
inline bool has_flag(FieldFlag flags, FieldFlag flag_to_check) {
  return (static_cast<uint32_t>(flags) &
          static_cast<uint32_t>(flag_to_check)) != 0;
}

// --- Field Metadata ---
struct FieldMeta {
  std::string db_name;
  std::string cpp_name;
  std::type_index cpp_type;
  std::string db_type_hint;
  FieldFlag flags = FieldFlag::None;
  std::any default_value;

  std::function<std::any(const void *)> getter;
  std::function<void(void *, const std::any &)> setter;

  FieldMeta(std::string dbName, std::string cppName, std::type_index cppType,
            std::string dbTypeHint = "", FieldFlag fieldFlags = FieldFlag::None,
            std::function<std::any(const void *)> g = nullptr,
            std::function<void(void *, const std::any &)> s = nullptr)
      : db_name(std::move(dbName)), cpp_name(std::move(cppName)),
        cpp_type(cppType), db_type_hint(std::move(dbTypeHint)),
        flags(fieldFlags), getter(std::move(g)), setter(std::move(s)) {}
};

// --- ModelBase Definition ---
class ModelBase {
public:
  virtual ~ModelBase() = default;

  [[nodiscard]] virtual const ModelMeta &_getOwnModelMeta() const = 0;
  [[nodiscard]] virtual std::string _getTableName() const = 0;
  [[nodiscard]] virtual std::map<std::string, std::any> _getPrimaryKeys() const;

  bool _is_persisted = false;

  std::any getFieldValue(const std::string &cpp_field_name) const;
  Error setFieldValue(const std::string &cpp_field_name, const std::any &value);

  virtual Error beforeCreate(Session & /*session*/) { return make_ok(); }
  virtual Error afterCreate(Session & /*session*/) { return make_ok(); }
  virtual Error beforeUpdate(Session & /*session*/) { return make_ok(); }
  virtual Error afterUpdate(Session & /*session*/) { return make_ok(); }
  virtual Error beforeSave(Session & /*session*/) { return make_ok(); }
  virtual Error afterSave(Session & /*session*/) { return make_ok(); }
  virtual Error beforeDelete(Session & /*session*/) { return make_ok(); }
  virtual Error afterDelete(Session & /*session*/) { return make_ok(); }
  virtual Error afterFind(Session & /*session*/) { return make_ok(); }
};

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
      if (f.db_name == name && !f.db_name.empty())
        return &f;
    return nullptr;
  }
  const FieldMeta *findFieldByCppName(const std::string &name) const {
    for (const auto &f : fields)
      if (f.cpp_name == name)
        return &f;
    return nullptr;
  }
  const AssociationMeta *
  findAssociationByCppName(const std::string &cpp_assoc_field_name) const {
    for (const auto &assoc : associations)
      if (assoc.cpp_field_name == cpp_assoc_field_name)
        return &assoc;
    return nullptr;
  }
  const FieldMeta *getPrimaryField(size_t idx = 0) const {
    if (primary_keys_db_names.empty() || idx >= primary_keys_db_names.size())
      return nullptr;
    return findFieldByDbName(primary_keys_db_names[idx]);
  }
  std::vector<const FieldMeta *> getPrimaryKeyFields() const {
    std::vector<const FieldMeta *> pks;
    pks.reserve(primary_keys_db_names.size());
    for (const auto &pk_name : primary_keys_db_names) {
      if (auto *f = findFieldByDbName(pk_name))
        pks.push_back(f);
    }
    return pks;
  }
  const FieldMeta *findFieldWithFlag(FieldFlag flag_to_find) const {
    auto it = std::find_if(fields.begin(), fields.end(),
                           [flag_to_find](const FieldMeta &fm) {
                             return has_flag(fm.flags, flag_to_find);
                           });
    return (it == fields.end()) ? nullptr : &(*it);
  }
};

inline std::any
ModelBase::getFieldValue(const std::string &cpp_field_name) const {
  const ModelMeta &meta = this->_getOwnModelMeta();
  const FieldMeta *field = meta.findFieldByCppName(cpp_field_name);
  if (!field) {
    const AssociationMeta *assoc =
        meta.findAssociationByCppName(cpp_field_name);
    if (assoc) {
      qWarning()
          << "cpporm ModelBase::getFieldValue: Attempted to get association "
             "collection or object '"
          << cpp_field_name.c_str()
          << "' via generic getter. Access the member directly after Preload.";
      return std::any{};
    }
    qWarning() << "cpporm ModelBase::getFieldValue: Field or Association "
                  "placeholder '"
               << cpp_field_name.c_str() << "' not found in meta for table "
               << QString::fromStdString(meta.table_name);
    return std::any{};
  }
  if (!field->getter) {
    qWarning() << "cpporm ModelBase::getFieldValue: Getter not found or "
                  "not finalized for field '"
               << cpp_field_name.c_str() << "' in table "
               << QString::fromStdString(meta.table_name);
    return std::any{};
  }
  return field->getter(this);
}

inline Error ModelBase::setFieldValue(const std::string &cpp_field_name,
                                      const std::any &value) {
  const ModelMeta &meta = this->_getOwnModelMeta();
  const FieldMeta *field = meta.findFieldByCppName(cpp_field_name);
  if (!field) {
    const AssociationMeta *assoc =
        meta.findAssociationByCppName(cpp_field_name);
    if (assoc) {
      qWarning() << "cpporm ModelBase::setFieldValue: Attempted to set "
                    "association collection or object '"
                 << cpp_field_name.c_str()
                 << "' via generic setter. This is usually handled by Preload "
                    "setters or direct member assignment if applicable.";
      return Error(ErrorCode::MappingError,
                   "Cannot set association via generic setFieldValue.");
    }
    qWarning() << "cpporm ModelBase::setFieldValue: Field or Association "
                  "placeholder '"
               << cpp_field_name.c_str() << "' not found in meta for table "
               << QString::fromStdString(meta.table_name);
    return Error(ErrorCode::MappingError, "Field or Association placeholder " +
                                              cpp_field_name + " not found.");
  }
  if (!field->setter) {
    qWarning() << "cpporm ModelBase::setFieldValue: Setter not found or "
                  "not finalized for field '"
               << cpp_field_name.c_str() << "' in table "
               << QString::fromStdString(meta.table_name);
    return Error(ErrorCode::MappingError,
                 "Setter for " + cpp_field_name + " not found/finalized.");
  }
  try {
    field->setter(this, value);
  } catch (const std::bad_any_cast &e) {
    qWarning() << "cpporm ModelBase::setFieldValue: Bad_any_cast for field '"
               << cpp_field_name.c_str()
               << "' (table: " << QString::fromStdString(meta.table_name)
               << ", expected C++ type: " << field->cpp_type.name()
               << ", value provided type: "
               << (value.has_value() ? value.type().name() : "empty_any")
               << "): " << e.what();
    return Error(ErrorCode::MappingError,
                 "Type mismatch for field " + cpp_field_name + ": " + e.what());
  } catch (const std::exception &e) {
    qWarning()
        << "cpporm ModelBase::setFieldValue: Exception while setting field '"
        << cpp_field_name.c_str()
        << "' (table: " << QString::fromStdString(meta.table_name)
        << "): " << e.what();
    return Error(ErrorCode::MappingError,
                 "Setter failed for field " + cpp_field_name + ": " + e.what());
  }
  return make_ok();
}

inline std::map<std::string, std::any> ModelBase::_getPrimaryKeys() const {
  std::map<std::string, std::any> pks;
  const auto &meta = this->_getOwnModelMeta();
  for (const auto &pk_db_name : meta.primary_keys_db_names) {
    const FieldMeta *fm = meta.findFieldByDbName(pk_db_name);
    if (fm && fm->getter) {
      try {
        pks[pk_db_name] = fm->getter(this);
      } catch (const std::exception &e) {
        qWarning()
            << "cpporm ModelBase::_getPrimaryKeys: Getter failed for PK field "
            << pk_db_name.c_str() << " on table "
            << QString::fromStdString(meta.table_name) << ": " << e.what();
      }
    } else {
      qWarning()
          << "cpporm ModelBase::_getPrimaryKeys: Primary key field meta or "
             "getter not found for DB name: "
          << pk_db_name.c_str() << " on table "
          << QString::fromStdString(meta.table_name);
    }
  }
  return pks;
}

namespace internal {
using ModelFactory = std::function<std::unique_ptr<ModelBase>()>;
std::map<std::type_index, ModelFactory> &getGlobalModelFactoryRegistry();
std::mutex &getGlobalModelFactoryRegistryMutex();

template <typename T> void registerModelFactory() {
  std::lock_guard<std::mutex> lock(getGlobalModelFactoryRegistryMutex());
  getGlobalModelFactoryRegistry()[typeid(T)] = []() {
    return std::make_unique<T>();
  };
}

using VoidFunc = std::function<void()>;
std::vector<VoidFunc> &getGlobalModelFinalizerFunctions();
std::mutex &getGlobalModelFinalizersRegistryMutex();

template <typename ModelClass> void registerModelClassForFinalization() {
  std::lock_guard<std::mutex> lock(getGlobalModelFinalizersRegistryMutex());
  getGlobalModelFinalizerFunctions().push_back(
      []() { ModelClass::_finalizeModelMeta(); });
}

} // namespace internal

void finalize_all_model_meta();

using FieldMetaProvider = std::function<FieldMeta()>;
// Renamed from AssociationMetaConfigurator to be more specific
// This provider now returns a partially filled AssociationMeta (without
// target_model_type) and the TargetTypeIndexProvider separately.
using PendingAssociationProvider = std::function<AssociationMeta()>;

using IndexDefinitionProvider = std::function<IndexDefinition()>;

template <typename Derived> class Model : public ModelBase {
public:
  inline static ModelMeta _shared_meta_instance;
  inline static std::vector<FieldMetaProvider> *_pending_field_meta_providers =
      nullptr;
  // Changed type for pending associations
  inline static std::vector<PendingAssociationProvider>
      *_pending_association_providers = nullptr;
  inline static std::vector<IndexDefinitionProvider>
      *_pending_index_definition_providers = nullptr;
  inline static std::mutex _meta_init_mutex;

  // New static function to get type_index of Derived
  static std::type_index _get_static_type_index() { return typeid(Derived); }

  static void _initSharedMetaTableName(const char *tableNameFromMacro) {
    std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
    if (Model<Derived>::_shared_meta_instance.table_name.empty() &&
        tableNameFromMacro && *tableNameFromMacro) {
      Model<Derived>::_shared_meta_instance.table_name = tableNameFromMacro;
      cpporm::internal::registerModelFactory<Derived>();
    }
  }

  static void _addPendingFieldMetaProvider(FieldMetaProvider provider) {
    std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
    if (!Model<Derived>::_pending_field_meta_providers)
      Model<Derived>::_pending_field_meta_providers =
          new std::vector<FieldMetaProvider>();
    Model<Derived>::_pending_field_meta_providers->push_back(
        std::move(provider));
  }

  static void
  _addPendingAssociationProvider(PendingAssociationProvider provider) {
    std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
    if (!Model<Derived>::_pending_association_providers) {
      Model<Derived>::_pending_association_providers =
          new std::vector<PendingAssociationProvider>();
    }
    Model<Derived>::_pending_association_providers->push_back(
        std::move(provider));
  }

  static void
  _addPendingIndexDefinitionProvider(IndexDefinitionProvider provider) {
    std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
    if (!Model<Derived>::_pending_index_definition_providers)
      Model<Derived>::_pending_index_definition_providers =
          new std::vector<IndexDefinitionProvider>();
    Model<Derived>::_pending_index_definition_providers->push_back(
        std::move(provider));
  }

  static void _finalizeModelMeta() {
    std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
    ModelMeta &s_meta = Model<Derived>::_shared_meta_instance;
    if (s_meta._is_finalized)
      return;

    if (Model<Derived>::_pending_field_meta_providers) {
      for (const auto &provider_func :
           *Model<Derived>::_pending_field_meta_providers)
        if (provider_func) {
          auto field_meta_obj = provider_func();
          bool already_exists = false;
          for (const auto &existing_field_meta : s_meta.fields)
            if (existing_field_meta.cpp_name == field_meta_obj.cpp_name)
              already_exists = true;
          if (!already_exists) {
            s_meta.fields.push_back(std::move(field_meta_obj));
            const auto &added_field_meta = s_meta.fields.back();
            if (has_flag(added_field_meta.flags, FieldFlag::PrimaryKey)) {
              bool pk_already_listed = false;
              for (const auto &pk_name_str : s_meta.primary_keys_db_names)
                if (pk_name_str == added_field_meta.db_name)
                  pk_already_listed = true;
              if (!pk_already_listed && !added_field_meta.db_name.empty())
                s_meta.primary_keys_db_names.push_back(
                    added_field_meta.db_name);
            }
          }
        }
      delete Model<Derived>::_pending_field_meta_providers;
      Model<Derived>::_pending_field_meta_providers = nullptr;
    }

    // Process pending associations
    if (Model<Derived>::_pending_association_providers) {
      for (const auto &provider_func :
           *Model<Derived>::_pending_association_providers) {
        if (provider_func) {
          AssociationMeta assoc_meta_obj = provider_func();
          // Now, resolve the target_model_type using the provider function
          if (assoc_meta_obj.target_type_index_provider) {
            assoc_meta_obj.target_model_type =
                assoc_meta_obj.target_type_index_provider();
          } else {
            qWarning() << "cpporm Model::finalizeModelMeta: Association"
                       << QString::fromStdString(assoc_meta_obj.cpp_field_name)
                       << "in model"
                       << QString::fromStdString(s_meta.table_name)
                       << "is missing a target type index provider.";
          }

          bool already_exists = false;
          for (const auto &existing_assoc_meta : s_meta.associations) {
            if (existing_assoc_meta.cpp_field_name ==
                assoc_meta_obj.cpp_field_name) {
              already_exists = true;
              break;
            }
          }
          if (!already_exists) {
            s_meta.associations.push_back(std::move(assoc_meta_obj));
          }
        }
      }
      delete Model<Derived>::_pending_association_providers;
      Model<Derived>::_pending_association_providers = nullptr;
    }

    if (Model<Derived>::_pending_index_definition_providers) {
      for (const auto &provider_func :
           *Model<Derived>::_pending_index_definition_providers)
        if (provider_func)
          s_meta.indexes.push_back(provider_func());
      delete Model<Derived>::_pending_index_definition_providers;
      Model<Derived>::_pending_index_definition_providers = nullptr;
    }
    s_meta._is_finalized = true;
  }

  static const ModelMeta &getModelMeta() {
    if (!Model<Derived>::_shared_meta_instance._is_finalized) {
      Model<Derived>::_finalizeModelMeta();
    }
    return Model<Derived>::_shared_meta_instance;
  }

  [[nodiscard]] const ModelMeta &_getOwnModelMeta() const final {
    return Model<Derived>::getModelMeta();
  }
  [[nodiscard]] std::string _getTableName() const final {
    return Model<Derived>::getModelMeta().table_name;
  }

  static std::unique_ptr<ModelBase> createInstance() {
    return std::make_unique<Derived>();
  }

  template <typename FieldType, FieldType Derived::*MemberPtr>
  static std::any _cpporm_generated_getter(const void *obj_ptr) {
    return static_cast<const Derived *>(obj_ptr)->*MemberPtr;
  }

  template <typename FieldType, FieldType Derived::*MemberPtr>
  static void _cpporm_generated_setter(void *obj_ptr, const std::any &value) {
    try {
      if (value.has_value()) {
        (static_cast<Derived *>(obj_ptr)->*MemberPtr) =
            std::any_cast<FieldType>(value);
      } else {
        (static_cast<Derived *>(obj_ptr)->*MemberPtr) = FieldType{};
      }
    } catch (const std::bad_any_cast &e) {
      qWarning() << "cpporm Model::generated_setter: Bad_any_cast for type "
                 << typeid(FieldType).name() << " from value of type "
                 << (value.has_value() ? value.type().name() : "empty_any")
                 << ". Details: " << e.what();
      throw;
    } catch (const std::exception &e) {
      qWarning() << "cpporm Model::generated_setter: Exception for type "
                 << typeid(FieldType).name() << ": " << e.what();
      throw;
    }
  }

  template <typename AssociatedModel,
            std::vector<std::shared_ptr<AssociatedModel>> Derived::*MemberPtr>
  static void _cpporm_generated_association_vector_setter(
      void *obj_ptr, std::vector<std::shared_ptr<ModelBase>>
                         &associated_models_base_sptr_vec) {
    Derived *model_instance = static_cast<Derived *>(obj_ptr);
    std::vector<std::shared_ptr<AssociatedModel>> &target_vector =
        model_instance->*MemberPtr;

    target_vector.clear();
    target_vector.reserve(associated_models_base_sptr_vec.size());

    for (auto &base_model_sptr : associated_models_base_sptr_vec) {
      if (!base_model_sptr)
        continue;

      std::shared_ptr<AssociatedModel> derived_sptr =
          std::dynamic_pointer_cast<AssociatedModel>(base_model_sptr);

      if (derived_sptr) {
        target_vector.push_back(derived_sptr);
      } else {
        qWarning() << "cpporm: Type mismatch in "
                      "_cpporm_generated_association_vector_setter. Expected "
                   << typeid(AssociatedModel).name()
                   << " but got different type "
                   << typeid(*base_model_sptr.get()).name()
                   << ". Object not added to target vector.";
      }
    }
  }

  template <typename AssociatedModel,
            std::shared_ptr<AssociatedModel> Derived::*MemberPtr>
  static void _cpporm_generated_association_single_setter(
      void *obj_ptr, std::shared_ptr<ModelBase> associated_model_base_sptr) {
    Derived *model_instance = static_cast<Derived *>(obj_ptr);

    if (!associated_model_base_sptr) {
      (model_instance->*MemberPtr) = nullptr;
      return;
    }

    std::shared_ptr<AssociatedModel> derived_sptr =
        std::dynamic_pointer_cast<AssociatedModel>(associated_model_base_sptr);

    if (derived_sptr) {
      (model_instance->*MemberPtr) = derived_sptr;
    } else {
      qWarning() << "cpporm: Type mismatch in "
                    "_cpporm_generated_association_single_setter. Expected "
                 << typeid(AssociatedModel).name() << " but got different type "
                 << typeid(*associated_model_base_sptr.get()).name()
                 << ". Object not set.";
      (model_instance->*MemberPtr) = nullptr;
    }
  }
};

#if __cplusplus < 201703L
template <typename Derived> ModelMeta Model<Derived>::_shared_meta_instance;
template <typename Derived>
std::vector<FieldMetaProvider> *Model<Derived>::_pending_field_meta_providers;
template <typename Derived>
std::vector<PendingAssociationProvider>
    *Model<Derived>::_pending_association_providers;
template <typename Derived>
std::vector<IndexDefinitionProvider>
    *Model<Derived>::_pending_index_definition_providers;
template <typename Derived> std::mutex Model<Derived>::_meta_init_mutex;
#endif

} // namespace cpporm
#endif // cpporm_MODEL_BASE_H