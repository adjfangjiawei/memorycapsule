// cpporm/session_preload_utils.cpp
#include "cpporm/i_query_executor.h"
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"

#include <QDebug>
#include <algorithm>
#include <any>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace cpporm {

static std::string
any_to_string_for_map_key_in_preload_util(const std::any &val) {
  if (!val.has_value())
    return "__cpporm_NULL_KEY__";

  const auto &type = val.type();
  if (type == typeid(int))
    return "i_" + std::to_string(std::any_cast<int>(val));
  if (type == typeid(long long))
    return "ll_" + std::to_string(std::any_cast<long long>(val));
  if (type == typeid(unsigned int))
    return "ui_" + std::to_string(std::any_cast<unsigned int>(val));
  if (type == typeid(unsigned long long))
    return "ull_" + std::to_string(std::any_cast<unsigned long long>(val));
  if (type == typeid(std::string))
    return "s_" + std::any_cast<std::string>(val);
  if (type == typeid(const char *))
    return "s_" + std::string(std::any_cast<const char *>(val));
  if (type == typeid(QDateTime))
    return "dt_" + std::any_cast<QDateTime>(val)
                       .toUTC()
                       .toString(Qt::ISODateWithMs)
                       .toStdString();
  if (type == typeid(QDate))
    return "d_" + std::any_cast<QDate>(val).toString(Qt::ISODate).toStdString();
  if (type == typeid(bool))
    return "b_" + std::string(std::any_cast<bool>(val) ? "true" : "false");
  if (type == typeid(double))
    return "dbl_" + std::to_string(std::any_cast<double>(val));
  if (type == typeid(float))
    return "flt_" + std::to_string(std::any_cast<float>(val));

  qWarning()
      << "any_to_string_for_map_key_in_preload_util: Unsupported std::any type "
      << " ('" << val.type().name()
      << "') for map key generation during preload. ";
  return "__cpporm_UNSUPPORTED_KEY_TYPE_" + std::string(val.type().name()) +
         "__";
}

Error Session::processPreloadsInternal(
    const QueryBuilder &qb, std::vector<ModelBase *> &parent_models_raw_ptr) {
  const std::vector<PreloadRequest> &preload_requests = qb.getPreloadRequests();
  const ModelMeta *main_model_meta_ptr = qb.getModelMeta();

  if (!main_model_meta_ptr) {
    return Error(ErrorCode::InvalidConfiguration,
                 "processPreloadsInternal: QueryBuilder has no ModelMeta for "
                 "main model.");
  }
  const ModelMeta &main_model_meta = *main_model_meta_ptr;

  if (preload_requests.empty() || parent_models_raw_ptr.empty()) {
    return make_ok();
  }

  for (const auto &request : preload_requests) {
    std::string association_to_load = request.association_cpp_field_name;
    if (association_to_load.find('.') != std::string::npos) {
      qWarning() << "Session::processPreloadsInternal: Nested preload paths "
                    "(e.g., 'Orders.Items') are not yet fully supported. "
                 << "Preloading only the first part of: '"
                 << QString::fromStdString(association_to_load) << "'";
      association_to_load =
          association_to_load.substr(0, association_to_load.find('.'));
    }

    const AssociationMeta *assoc_meta =
        main_model_meta.findAssociationByCppName(association_to_load);
    if (!assoc_meta) {
      qWarning() << "Session::processPreloadsInternal: Association '"
                 << QString::fromStdString(association_to_load)
                 << "' not found in model '"
                 << QString::fromStdString(main_model_meta.table_name)
                 << "' for preloading.";
      continue;
    }

    Error err = executePreloadForAssociation(*assoc_meta, main_model_meta,
                                             parent_models_raw_ptr);
    if (err) {
      qWarning() << "Session::processPreloadsInternal: Error preloading "
                    "association '"
                 << QString::fromStdString(association_to_load)
                 << "': " << QString::fromStdString(err.toString());
      return err;
    }
  }
  return make_ok();
}

Error Session::processPreloads(
    const QueryBuilder &qb,
    std::vector<std::unique_ptr<ModelBase>> &loaded_models_unique_ptr) {
  if (loaded_models_unique_ptr.empty()) {
    return make_ok();
  }
  std::vector<ModelBase *> raw_ptr_vec;
  raw_ptr_vec.reserve(loaded_models_unique_ptr.size());
  for (const auto &u_ptr : loaded_models_unique_ptr) {
    if (u_ptr) {
      raw_ptr_vec.push_back(u_ptr.get());
    }
  }
  if (raw_ptr_vec.empty() && !loaded_models_unique_ptr.empty()) {
    return make_ok();
  }
  return processPreloadsInternal(qb, raw_ptr_vec);
}

Error Session::executePreloadForAssociation(
    const AssociationMeta &assoc_meta, const ModelMeta &parent_model_meta,
    std::vector<ModelBase *> &parent_models_raw_ptr) {

  if (parent_models_raw_ptr.empty()) {
    return make_ok();
  }

  cpporm::internal::ModelFactory target_model_factory_fn;
  {
    std::lock_guard<std::mutex> lock(
        internal::getGlobalModelFactoryRegistryMutex());
    auto it_factory = internal::getGlobalModelFactoryRegistry().find(
        assoc_meta.target_model_type);
    if (it_factory == internal::getGlobalModelFactoryRegistry().end()) {
      return Error(
          ErrorCode::InternalError,
          "Preload Error: Target model factory not found for type_index: " +
              std::string(assoc_meta.target_model_type.name()) +
              " for association '" + assoc_meta.cpp_field_name + "'.");
    }
    target_model_factory_fn = it_factory->second;
  }

  std::unique_ptr<ModelBase> temp_target_instance_uptr =
      target_model_factory_fn();
  if (!temp_target_instance_uptr) {
    return Error(ErrorCode::InternalError,
                 "Preload Error: Target model factory failed to create "
                 "instance for type: " +
                     std::string(assoc_meta.target_model_type.name()) +
                     " for association '" + assoc_meta.cpp_field_name + "'.");
  }
  const ModelMeta &target_model_meta =
      temp_target_instance_uptr->_getOwnModelMeta();
  if (target_model_meta.table_name.empty()) {
    return Error(ErrorCode::InvalidConfiguration,
                 "Preload Error: Target model '" +
                     std::string(assoc_meta.target_model_type.name()) +
                     "' has an empty table name in its metadata.");
  }

  std::string parent_model_key_cpp_name;
  std::string target_model_key_db_name;

  if (assoc_meta.type == AssociationType::HasMany ||
      assoc_meta.type == AssociationType::HasOne) {
    const FieldMeta *pk_field_on_parent = nullptr;
    if (!assoc_meta.primary_key_db_name_on_current_model.empty()) {
      pk_field_on_parent = parent_model_meta.findFieldByDbName(
          assoc_meta.primary_key_db_name_on_current_model);
    } else if (!parent_model_meta.primary_keys_db_names.empty()) {
      pk_field_on_parent = parent_model_meta.findFieldByDbName(
          parent_model_meta.primary_keys_db_names[0]);
    } else {
      return Error(ErrorCode::MappingError,
                   "Preload Error (HasMany/HasOne): Parent model '" +
                       parent_model_meta.table_name +
                       "' has no primary keys defined for association '" +
                       assoc_meta.cpp_field_name + "'.");
    }
    if (!pk_field_on_parent) {
      return Error(
          ErrorCode::MappingError,
          "Preload Error (HasMany/HasOne): Parent reference key DB name '" +
              (assoc_meta.primary_key_db_name_on_current_model.empty()
                   ? (parent_model_meta.primary_keys_db_names.empty()
                          ? "[UNKNOWN_PK]"
                          : parent_model_meta.primary_keys_db_names[0])
                   : assoc_meta.primary_key_db_name_on_current_model) +
              "' not found on parent model '" + parent_model_meta.table_name +
              "' for association '" + assoc_meta.cpp_field_name + "'.");
    }
    parent_model_key_cpp_name = pk_field_on_parent->cpp_name;
    target_model_key_db_name = assoc_meta.foreign_key_db_name;
    if (target_model_key_db_name.empty()) {
      return Error(ErrorCode::MappingError,
                   "Preload Error (HasMany/HasOne): Foreign key on target "
                   "model not specified for association '" +
                       assoc_meta.cpp_field_name + "'.");
    }
  } else if (assoc_meta.type == AssociationType::BelongsTo) {
    const FieldMeta *fk_field_on_parent =
        parent_model_meta.findFieldByDbName(assoc_meta.foreign_key_db_name);
    if (!fk_field_on_parent) {
      return Error(ErrorCode::MappingError,
                   "Preload Error (BelongsTo): Foreign key DB name '" +
                       assoc_meta.foreign_key_db_name +
                       "' not found on parent model '" +
                       parent_model_meta.table_name + "' for association '" +
                       assoc_meta.cpp_field_name + "'.");
    }
    parent_model_key_cpp_name = fk_field_on_parent->cpp_name;
    if (!assoc_meta.target_model_pk_db_name.empty()) {
      target_model_key_db_name = assoc_meta.target_model_pk_db_name;
    } else if (!target_model_meta.primary_keys_db_names.empty()) {
      target_model_key_db_name = target_model_meta.primary_keys_db_names[0];
    } else {
      return Error(ErrorCode::MappingError,
                   "Preload Error (BelongsTo): Target model '" +
                       target_model_meta.table_name +
                       "' has no primary keys defined for association '" +
                       assoc_meta.cpp_field_name + "'.");
    }
    if (target_model_key_db_name.empty()) {
      return Error(ErrorCode::MappingError,
                   "Preload Error (BelongsTo): Referenced key on target model "
                   "not specified or determinable for association '" +
                       assoc_meta.cpp_field_name + "'.");
    }
  } else if (assoc_meta.type == AssociationType::ManyToMany) {
    return Error(ErrorCode::UnsupportedFeature,
                 "Preload Error: ManyToMany preloading for association '" +
                     assoc_meta.cpp_field_name + "' is not yet implemented.");
  } else {
    return Error(ErrorCode::InternalError,
                 "Preload Error: Unknown association type for '" +
                     assoc_meta.cpp_field_name + "'.");
  }

  if (parent_model_key_cpp_name.empty() || target_model_key_db_name.empty()) {
    return Error(ErrorCode::MappingError,
                 "Preload Error: Could not determine one or both join key "
                 "names for association '" +
                     assoc_meta.cpp_field_name + "'. ParentKeyCppName: '" +
                     parent_model_key_cpp_name + "', TargetKeyDbName: '" +
                     target_model_key_db_name + "'.");
  }

  std::vector<QueryValue> parent_key_values_for_in_clause;
  parent_key_values_for_in_clause.reserve(parent_models_raw_ptr.size());
  for (const auto parent_model_ptr : parent_models_raw_ptr) {
    if (parent_model_ptr) {
      std::any key_any =
          parent_model_ptr->getFieldValue(parent_model_key_cpp_name);
      if (key_any.has_value()) {
        QueryValue qv = Session::anyToQueryValueForSessionConvenience(key_any);
        if (std::holds_alternative<std::nullptr_t>(qv) && key_any.has_value()) {
          qWarning() << "Preload Warning: Unsupported parent key type ('"
                     << key_any.type().name()
                     << "') for IN clause when preloading '"
                     << QString::fromStdString(assoc_meta.cpp_field_name)
                     << "'. Skipping key value.";
          continue;
        }
        if (!std::holds_alternative<std::nullptr_t>(qv)) {
          parent_key_values_for_in_clause.push_back(qv);
        }
      }
    }
  }

  if (parent_key_values_for_in_clause.empty()) {
    return make_ok();
  }

  QueryBuilder qb_preload(this, this->connection_name_, &target_model_meta);

  std::string quoted_target_key_db_name =
      QueryBuilder::quoteSqlIdentifier(target_model_key_db_name);
  if (parent_key_values_for_in_clause.empty()) {
    return make_ok();
  }
  std::string in_placeholders_sql_segment;
  for (size_t i = 0; i < parent_key_values_for_in_clause.size(); ++i) {
    in_placeholders_sql_segment += (i == 0 ? "?" : ", ?");
  }
  qb_preload.Where(quoted_target_key_db_name + " IN (" +
                       in_placeholders_sql_segment + ")",
                   parent_key_values_for_in_clause);

  std::vector<std::unique_ptr<ModelBase>>
      associated_results_unique_ptr_vec; // Correct type for FindImpl
  Error find_err = this->FindImpl(qb_preload, associated_results_unique_ptr_vec,
                                  target_model_factory_fn);

  if (find_err) {
    return Error(find_err.code,
                 "Preload Error: Failed to fetch associated models for '" +
                     assoc_meta.cpp_field_name + "' from table '" +
                     target_model_meta.table_name + "': " + find_err.message);
  }

  if (associated_results_unique_ptr_vec.empty()) {
    for (auto parent_model_ptr : parent_models_raw_ptr) {
      if (!parent_model_ptr)
        continue;
      if (assoc_meta.type == AssociationType::HasMany &&
          assoc_meta.data_setter_vector) {
        std::vector<std::shared_ptr<ModelBase>> empty_vec;
        assoc_meta.data_setter_vector(parent_model_ptr, empty_vec);
      } else if ((assoc_meta.type == AssociationType::HasOne ||
                  assoc_meta.type == AssociationType::BelongsTo) &&
                 assoc_meta.data_setter_single) {
        assoc_meta.data_setter_single(parent_model_ptr, nullptr);
      }
    }
    return make_ok();
  }

  const FieldMeta *target_model_key_field_meta =
      target_model_meta.findFieldByDbName(target_model_key_db_name);
  if (!target_model_key_field_meta) {
    return Error(ErrorCode::MappingError,
                 "Preload Error: Target model's join key C++ field meta not "
                 "found for DB name: '" +
                     target_model_key_db_name + "' on table '" +
                     target_model_meta.table_name + "'. Cannot map results.");
  }

  std::map<std::string, std::vector<std::shared_ptr<ModelBase>>>
      map_associated_by_their_link_key_value;
  for (auto &assoc_model_uptr : associated_results_unique_ptr_vec) {
    if (assoc_model_uptr) {
      std::any link_key_val_any = assoc_model_uptr->getFieldValue(
          target_model_key_field_meta->cpp_name);
      std::string link_key_val_str_key =
          any_to_string_for_map_key_in_preload_util(link_key_val_any);

      if (link_key_val_str_key.rfind("__cpporm_UNSUPPORTED_KEY_TYPE_", 0) ==
              0 ||
          link_key_val_str_key == "__cpporm_NULL_KEY__") {
        // Corrected variable name: assoc_model_uptr is the current item.
        const ModelMeta &t_assoc_meta =
            assoc_model_uptr->_getOwnModelMeta(); // Was current_assoc_meta
        const FieldMeta *current_assoc_pk_field =
            t_assoc_meta.getPrimaryField(); // Use t_assoc_meta
        std::string current_assoc_pk_val_str = "N/A_PK_FIELD";
        if (current_assoc_pk_field) {
          current_assoc_pk_val_str = any_to_string_for_map_key_in_preload_util(
              assoc_model_uptr->getFieldValue(
                  current_assoc_pk_field->cpp_name));
        }

        qWarning() << "Preload Warning: Could not get or convert foreign key "
                      "value to string for mapping associated model for "
                   << QString::fromStdString(assoc_meta.cpp_field_name)
                   << ". Target model PK (if available): "
                   << QString::fromStdString(current_assoc_pk_val_str);
        continue;
      }
      map_associated_by_their_link_key_value[link_key_val_str_key].push_back(
          std::move(assoc_model_uptr));
    }
  }
  associated_results_unique_ptr_vec.clear();

  for (auto parent_model_ptr : parent_models_raw_ptr) {
    if (!parent_model_ptr)
      continue;
    std::any parent_key_val_any =
        parent_model_ptr->getFieldValue(parent_model_key_cpp_name);
    std::string parent_key_val_str_key =
        any_to_string_for_map_key_in_preload_util(parent_key_val_any);

    auto it_found_associated =
        map_associated_by_their_link_key_value.find(parent_key_val_str_key);

    if (it_found_associated != map_associated_by_their_link_key_value.end()) {
      std::vector<std::shared_ptr<ModelBase>>
          &associated_for_this_parent_sptrs = it_found_associated->second;
      if (assoc_meta.type == AssociationType::HasMany) {
        if (assoc_meta.data_setter_vector) {
          assoc_meta.data_setter_vector(parent_model_ptr,
                                        associated_for_this_parent_sptrs);
        } else {
          qWarning()
              << "Preload: Missing vector setter for HasMany association "
              << QString::fromStdString(assoc_meta.cpp_field_name)
              << " on parent "
              << QString::fromStdString(parent_model_meta.table_name);
        }
      } else if (assoc_meta.type == AssociationType::HasOne ||
                 assoc_meta.type == AssociationType::BelongsTo) {
        if (assoc_meta.data_setter_single) {
          if (!associated_for_this_parent_sptrs.empty()) {
            assoc_meta.data_setter_single(
                parent_model_ptr, associated_for_this_parent_sptrs.front());
          } else {
            assoc_meta.data_setter_single(parent_model_ptr, nullptr);
          }
        } else {
          qWarning() << "Preload: Missing single setter for HasOne/BelongsTo "
                        "association "
                     << QString::fromStdString(assoc_meta.cpp_field_name)
                     << " on parent "
                     << QString::fromStdString(parent_model_meta.table_name);
        }
      }
    } else {
      if (assoc_meta.type == AssociationType::HasMany &&
          assoc_meta.data_setter_vector) {
        std::vector<std::shared_ptr<ModelBase>> empty_sptr_vec;
        assoc_meta.data_setter_vector(parent_model_ptr, empty_sptr_vec);
      } else if ((assoc_meta.type == AssociationType::HasOne ||
                  assoc_meta.type == AssociationType::BelongsTo) &&
                 assoc_meta.data_setter_single) {
        assoc_meta.data_setter_single(parent_model_ptr, nullptr);
      }
    }
  }
  return make_ok();
}

} // namespace cpporm