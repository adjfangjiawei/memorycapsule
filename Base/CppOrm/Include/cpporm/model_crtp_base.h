#ifndef cpporm_MODEL_CRTP_BASE_H
#define cpporm_MODEL_CRTP_BASE_H

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "cpporm/model_base_class.h"
#include "cpporm/model_registry.h"  // For registration functions

namespace cpporm {

    using FieldMetaProvider = std::function<FieldMeta()>;
    // Renamed from AssociationMetaConfigurator to be more specific
    // This provider now returns a partially filled AssociationMeta (without
    // target_model_type) and the TargetTypeIndexProvider separately.
    using PendingAssociationProvider = std::function<AssociationMeta()>;

    using IndexDefinitionProvider = std::function<IndexDefinition()>;

    template <typename Derived>
    class Model : public ModelBase {
      public:
        inline static ModelMeta _shared_meta_instance;
        inline static std::vector<FieldMetaProvider> *_pending_field_meta_providers = nullptr;
        // Changed type for pending associations
        inline static std::vector<PendingAssociationProvider> *_pending_association_providers = nullptr;
        inline static std::vector<IndexDefinitionProvider> *_pending_index_definition_providers = nullptr;
        inline static std::mutex _meta_init_mutex;

        // New static function to get type_index of Derived
        static std::type_index _get_static_type_index() {
            return typeid(Derived);
        }

        static void _initSharedMetaTableName(const char *tableNameFromMacro) {
            std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
            if (Model<Derived>::_shared_meta_instance.table_name.empty() && tableNameFromMacro && *tableNameFromMacro) {
                Model<Derived>::_shared_meta_instance.table_name = tableNameFromMacro;
                cpporm::internal::registerModelFactory<Derived>();
            }
        }

        static void _addPendingFieldMetaProvider(FieldMetaProvider provider) {
            std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
            if (!Model<Derived>::_pending_field_meta_providers) Model<Derived>::_pending_field_meta_providers = new std::vector<FieldMetaProvider>();
            Model<Derived>::_pending_field_meta_providers->push_back(std::move(provider));
        }

        static void _addPendingAssociationProvider(PendingAssociationProvider provider) {
            std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
            if (!Model<Derived>::_pending_association_providers) {
                Model<Derived>::_pending_association_providers = new std::vector<PendingAssociationProvider>();
            }
            Model<Derived>::_pending_association_providers->push_back(std::move(provider));
        }

        static void _addPendingIndexDefinitionProvider(IndexDefinitionProvider provider) {
            std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
            if (!Model<Derived>::_pending_index_definition_providers) Model<Derived>::_pending_index_definition_providers = new std::vector<IndexDefinitionProvider>();
            Model<Derived>::_pending_index_definition_providers->push_back(std::move(provider));
        }

        static void _finalizeModelMeta() {
            std::lock_guard<std::mutex> lock(Model<Derived>::_meta_init_mutex);
            ModelMeta &s_meta = Model<Derived>::_shared_meta_instance;
            if (s_meta._is_finalized) return;

            if (Model<Derived>::_pending_field_meta_providers) {
                for (const auto &provider_func : *Model<Derived>::_pending_field_meta_providers)
                    if (provider_func) {
                        auto field_meta_obj = provider_func();
                        bool already_exists = false;
                        for (const auto &existing_field_meta : s_meta.fields)
                            if (existing_field_meta.cpp_name == field_meta_obj.cpp_name) already_exists = true;
                        if (!already_exists) {
                            s_meta.fields.push_back(std::move(field_meta_obj));
                            const auto &added_field_meta = s_meta.fields.back();
                            if (has_flag(added_field_meta.flags, FieldFlag::PrimaryKey)) {
                                bool pk_already_listed = false;
                                for (const auto &pk_name_str : s_meta.primary_keys_db_names)
                                    if (pk_name_str == added_field_meta.db_name) pk_already_listed = true;
                                if (!pk_already_listed && !added_field_meta.db_name.empty()) s_meta.primary_keys_db_names.push_back(added_field_meta.db_name);
                            }
                        }
                    }
                delete Model<Derived>::_pending_field_meta_providers;
                Model<Derived>::_pending_field_meta_providers = nullptr;
            }

            // Process pending associations
            if (Model<Derived>::_pending_association_providers) {
                for (const auto &provider_func : *Model<Derived>::_pending_association_providers) {
                    if (provider_func) {
                        AssociationMeta assoc_meta_obj = provider_func();
                        // Now, resolve the target_model_type using the provider function
                        if (assoc_meta_obj.target_type_index_provider) {
                            assoc_meta_obj.target_model_type = assoc_meta_obj.target_type_index_provider();
                        } else {
                            qWarning() << "cpporm Model::finalizeModelMeta: Association" << QString::fromStdString(assoc_meta_obj.cpp_field_name) << "in model" << QString::fromStdString(s_meta.table_name) << "is missing a target type index provider.";
                        }

                        bool already_exists = false;
                        for (const auto &existing_assoc_meta : s_meta.associations) {
                            if (existing_assoc_meta.cpp_field_name == assoc_meta_obj.cpp_field_name) {
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
                for (const auto &provider_func : *Model<Derived>::_pending_index_definition_providers)
                    if (provider_func) s_meta.indexes.push_back(provider_func());
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
                    (static_cast<Derived *>(obj_ptr)->*MemberPtr) = std::any_cast<FieldType>(value);
                } else {
                    (static_cast<Derived *>(obj_ptr)->*MemberPtr) = FieldType{};
                }
            } catch (const std::bad_any_cast &e) {
                qWarning() << "cpporm Model::generated_setter: Bad_any_cast for type " << typeid(FieldType).name() << " from value of type " << (value.has_value() ? value.type().name() : "empty_any") << ". Details: " << e.what();
                throw;
            } catch (const std::exception &e) {
                qWarning() << "cpporm Model::generated_setter: Exception for type " << typeid(FieldType).name() << ": " << e.what();
                throw;
            }
        }

        template <typename AssociatedModel, std::vector<std::shared_ptr<AssociatedModel>> Derived::*MemberPtr>
        static void _cpporm_generated_association_vector_setter(void *obj_ptr, std::vector<std::shared_ptr<ModelBase>> &associated_models_base_sptr_vec) {
            Derived *model_instance = static_cast<Derived *>(obj_ptr);
            std::vector<std::shared_ptr<AssociatedModel>> &target_vector = model_instance->*MemberPtr;

            target_vector.clear();
            target_vector.reserve(associated_models_base_sptr_vec.size());

            for (auto &base_model_sptr : associated_models_base_sptr_vec) {
                if (!base_model_sptr) continue;

                std::shared_ptr<AssociatedModel> derived_sptr = std::dynamic_pointer_cast<AssociatedModel>(base_model_sptr);

                if (derived_sptr) {
                    target_vector.push_back(derived_sptr);
                } else {
                    qWarning() << "cpporm: Type mismatch in "
                                  "_cpporm_generated_association_vector_setter. Expected "
                               << typeid(AssociatedModel).name() << " but got different type " << typeid(*base_model_sptr.get()).name() << ". Object not added to target vector.";
                }
            }
        }

        template <typename AssociatedModel, std::shared_ptr<AssociatedModel> Derived::*MemberPtr>
        static void _cpporm_generated_association_single_setter(void *obj_ptr, std::shared_ptr<ModelBase> associated_model_base_sptr) {
            Derived *model_instance = static_cast<Derived *>(obj_ptr);

            if (!associated_model_base_sptr) {
                (model_instance->*MemberPtr) = nullptr;
                return;
            }

            std::shared_ptr<AssociatedModel> derived_sptr = std::dynamic_pointer_cast<AssociatedModel>(associated_model_base_sptr);

            if (derived_sptr) {
                (model_instance->*MemberPtr) = derived_sptr;
            } else {
                qWarning() << "cpporm: Type mismatch in "
                              "_cpporm_generated_association_single_setter. Expected "
                           << typeid(AssociatedModel).name() << " but got different type " << typeid(*associated_model_base_sptr.get()).name() << ". Object not set.";
                (model_instance->*MemberPtr) = nullptr;
            }
        }
    };

#if __cplusplus < 201703L
    template <typename Derived>
    ModelMeta Model<Derived>::_shared_meta_instance;
    template <typename Derived>
    std::vector<FieldMetaProvider> *Model<Derived>::_pending_field_meta_providers;
    template <typename Derived>
    std::vector<PendingAssociationProvider> *Model<Derived>::_pending_association_providers;
    template <typename Derived>
    std::vector<IndexDefinitionProvider> *Model<Derived>::_pending_index_definition_providers;
    template <typename Derived>
    std::mutex Model<Derived>::_meta_init_mutex;
#endif

}  // namespace cpporm

#endif  // cpporm_MODEL_CRTP_BASE_H