// cpporm/session_save_op.cpp
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"

#include <QDateTime>
#include <QDebug>
#include <QMetaType>
#include <QSqlQuery>
#include <QVariant>

namespace cpporm {

// Helper to convert QVariant to QueryValue, specific for SaveOp needs.
QueryValue qvariantToQueryValueForSaveOpHelper(const QVariant &qv) {
  if (qv.isNull() || !qv.isValid()) {
    return nullptr;
  }

  QMetaType::Type effectiveTypeId;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  effectiveTypeId = static_cast<QMetaType::Type>(qv.typeId());
#else
  effectiveTypeId = static_cast<QMetaType::Type>(qv.type());
#endif
  if (effectiveTypeId == QMetaType::UnknownType &&
      qv.userType() != QMetaType::UnknownType) {
    effectiveTypeId = static_cast<QMetaType::Type>(qv.userType());
  }

  switch (effectiveTypeId) {
  case QMetaType::Int:
    return qv.toInt();
  case QMetaType::LongLong:
    return qv.toLongLong();
  case QMetaType::ULongLong:
    return static_cast<long long>(
        qv.toULongLong()); // Potential truncation if too large, QueryValue uses
                           // signed
  case QMetaType::UInt:
    return static_cast<int>(qv.toUInt()); // Potential truncation
  case QMetaType::Double:
    return qv.toDouble();
  case QMetaType::QString:
    return qv.toString().toStdString();
  case QMetaType::Bool:
    return qv.toBool();
  case QMetaType::QDateTime:
    return qv.toDateTime();
  case QMetaType::QDate:
    return qv.toDate();
  case QMetaType::QTime:
    return qv.toTime();
  case QMetaType::QByteArray:
    return qv.toByteArray();
  case QMetaType::Float:
    return static_cast<double>(
        qv.toFloat()); // Promote float to double for QueryValue
  default:
    qWarning() << "qvariantToQueryValueForSaveOpHelper: Unhandled QVariant "
                  "metaType for QueryValue conversion:"
               << qv.typeName()
               << "(TypeId: " << static_cast<int>(effectiveTypeId)
               << ", UserType: " << qv.userType() << ")";
    return nullptr;
  }
}

std::map<std::string, QueryValue>
convertQStringVariantMapToStringQueryValueMapForSaveOpHelper(
    const std::map<QString, QVariant> &qtMap) {
  std::map<std::string, QueryValue> result;
  for (const auto &pair : qtMap) {
    QueryValue q_val = qvariantToQueryValueForSaveOpHelper(pair.second);

    bool conversion_failed_for_valid_qvariant =
        std::holds_alternative<std::nullptr_t>(q_val) &&
        pair.second.isValid() && !pair.second.isNull();

    if (!conversion_failed_for_valid_qvariant) {
      result[pair.first.toStdString()] = q_val;
    } else {
      qWarning() << "convertQStringVariantMapToStringQueryValueMapForSaveOpHelp"
                    "er: Failed to convert QVariant for key"
                 << pair.first << "to QueryValue. Original QVariant type: "
                 << pair.second.typeName() << ". Skipping this field in map.";
    }
  }
  return result;
}

std::expected<long long, Error> Session::SaveImpl(
    const QueryBuilder
        &qb_param, // QB used for model meta and potentially OnConflict from QB
    ModelBase &model_instance) {
  const ModelMeta *meta_from_qb = qb_param.getModelMeta();
  const ModelMeta &meta =
      meta_from_qb ? *meta_from_qb : model_instance._getOwnModelMeta();

  if (meta.table_name.empty()) {
    return std::unexpected(
        Error(ErrorCode::InvalidConfiguration,
              "SaveImpl: ModelMeta does not have a valid table_name."));
  }

  Error hook_err = model_instance.beforeSave(*this);
  if (hook_err)
    return std::unexpected(hook_err);

  bool has_defined_pk = !meta.primary_keys_db_names.empty();
  bool model_has_all_pks_set_and_non_default = false;
  if (has_defined_pk) {
    model_has_all_pks_set_and_non_default = true;
    for (const auto &pk_db_name : meta.primary_keys_db_names) {
      const FieldMeta *pk_field = meta.findFieldByDbName(pk_db_name);
      if (pk_field) {
        std::any pk_val_any = model_instance.getFieldValue(pk_field->cpp_name);
        if (!pk_val_any.has_value()) {
          model_has_all_pks_set_and_non_default = false;
          break;
        }
        // Check for "zero" or "empty" values that GORM might consider as "not
        // set" for update decisions
        if (pk_val_any.type() == typeid(int) &&
            std::any_cast<int>(pk_val_any) == 0) {
          model_has_all_pks_set_and_non_default = false;
          break;
        } else if (pk_val_any.type() == typeid(long long) &&
                   std::any_cast<long long>(pk_val_any) == 0) {
          model_has_all_pks_set_and_non_default = false;
          break;
        } else if (pk_val_any.type() == typeid(unsigned int) &&
                   std::any_cast<unsigned int>(pk_val_any) == 0) {
          model_has_all_pks_set_and_non_default = false;
          break;
        } else if (pk_val_any.type() == typeid(unsigned long long) &&
                   std::any_cast<unsigned long long>(pk_val_any) == 0) {
          model_has_all_pks_set_and_non_default = false;
          break;
        } else if (pk_val_any.type() == typeid(std::string) &&
                   std::any_cast<std::string>(pk_val_any).empty()) {
          model_has_all_pks_set_and_non_default = false;
          break;
        }
      } else {
        model_has_all_pks_set_and_non_default = false; // PK meta missing
        qWarning() << "SaveImpl: PK field meta not found for"
                   << QString::fromStdString(pk_db_name);
        break;
      }
    }
  }

  // GORM's Save logic:
  // If primary key has non-zero value, it updates; otherwise, it creates.
  // _is_persisted can also hint, but PK value is often the primary driver.
  bool attempt_update =
      (model_instance._is_persisted || model_has_all_pks_set_and_non_default) &&
      has_defined_pk;

  if (attempt_update) {                                   // Attempt an UPDATE
    this->autoSetTimestamps(model_instance, meta, false); // Set updated_at
    cpporm::internal::SessionModelDataForWrite data_to_write =
        this->extractModelData(model_instance, meta, true, true);

    if (data_to_write.primary_key_fields.empty() && has_defined_pk) {
      return std::unexpected(
          Error(ErrorCode::MappingError,
                "SaveImpl (Update path): Failed to extract valid primary key "
                "values for WHERE clause."));
    }

    bool has_fields_to_set = !data_to_write.fields_to_write.empty();
    if (!has_fields_to_set) {
      qInfo("SaveImpl (Update path): No fields (including timestamps) to "
            "update for table %s. Skipping DB operation.",
            meta.table_name.c_str());
      // Still call hooks as the intent was to save/update
      hook_err = model_instance.beforeUpdate(
          *this); // Call beforeUpdate even if no DB op
      if (hook_err)
        return std::unexpected(hook_err);
      hook_err = model_instance.afterUpdate(*this);
      if (hook_err)
        return std::unexpected(hook_err);
      hook_err = model_instance.afterSave(*this);
      if (hook_err)
        return std::unexpected(hook_err);
      return 0LL; // 0 rows affected
    }

    hook_err = model_instance.beforeUpdate(*this);
    if (hook_err)
      return std::unexpected(hook_err);

    QueryBuilder update_qb(this, this->connection_name_, &meta);
    for (const auto &pk_name_std : meta.primary_keys_db_names) {
      QString pk_name_qstr = QString::fromStdString(pk_name_std);
      auto it = data_to_write.primary_key_fields.find(pk_name_qstr);
      if (it != data_to_write.primary_key_fields.end() &&
          it->second.isValid() && !it->second.isNull()) {
        update_qb.Where(pk_name_std + " = ?",
                        {qvariantToQueryValueForSaveOpHelper(it->second)});
      } else {
        return std::unexpected(Error(
            ErrorCode::MappingError,
            "SaveImpl (Update path): PK '" + pk_name_std +
                "' missing or invalid in extracted PKs for WHERE clause."));
      }
    }

    std::map<std::string, QueryValue> updates_for_impl =
        convertQStringVariantMapToStringQueryValueMapForSaveOpHelper(
            data_to_write.fields_to_write);

    // Remove PKs from the SET clause if they somehow got in (extractModelData
    // should handle this for `for_update=true`)
    for (const auto &pk_db_name : meta.primary_keys_db_names) {
      updates_for_impl.erase(pk_db_name);
    }
    if (updates_for_impl.empty()) { // If only PKs were in fields_to_write and
                                    // now it's empty
      qInfo("SaveImpl (Update path): After removing PKs, no fields left to "
            "update for table %s. Skipping DB operation.",
            meta.table_name.c_str());
      hook_err = model_instance.afterUpdate(*this);
      if (hook_err)
        return std::unexpected(hook_err);
      hook_err = model_instance.afterSave(*this);
      if (hook_err)
        return std::unexpected(hook_err);
      return 0LL;
    }

    auto update_result = this->UpdatesImpl(update_qb, updates_for_impl);

    if (!update_result.has_value())
      return std::unexpected(update_result.error());

    if (update_result.value() >= 0)
      model_instance._is_persisted = true;

    hook_err = model_instance.afterUpdate(*this);
    if (hook_err)
      return std::unexpected(hook_err);
    hook_err = model_instance.afterSave(*this);
    if (hook_err)
      return std::unexpected(hook_err);

    return update_result.value();

  } else { // Attempt a CREATE
    hook_err = model_instance.beforeCreate(*this);
    if (hook_err)
      return std::unexpected(hook_err);

    this->autoSetTimestamps(model_instance, meta, true);

    const OnConflictClause *final_conflict_options = nullptr;
    std::unique_ptr<OnConflictClause>
        save_upsert_clause_ptr; // Use unique_ptr for ownership

    if (qb_param
            .getOnConflictClause()) { // 1. From QueryBuilder passed to SaveImpl
      final_conflict_options = qb_param.getOnConflictClause();
    } else if (this->getTempOnConflictClause()) { // 2. From Session's temporary
                                                  // state
      final_conflict_options = this->getTempOnConflictClause();
    } else if (has_defined_pk && model_has_all_pks_set_and_non_default) {
      // 3. Default for Save: if PKs are set, behave like UPSERT (update all
      // non-PK) This implies an "ON DUPLICATE KEY UPDATE" or "ON CONFLICT DO
      // UPDATE" behavior.
      save_upsert_clause_ptr = std::make_unique<OnConflictClause>(
          OnConflictClause::Action::UpdateAllExcluded);
      // For PostgreSQL, UpdateAllExcluded needs conflict targets.
      // If model_meta has PKs, they are good default targets.
      if (!meta.primary_keys_db_names.empty() &&
          save_upsert_clause_ptr->conflict_target_columns_db_names.empty()) {
        save_upsert_clause_ptr->conflict_target_columns_db_names =
            meta.primary_keys_db_names;
      }
      final_conflict_options = save_upsert_clause_ptr.get();
    }
    // If no PKs defined or PKs are zero/default, it's a pure INSERT,
    // final_conflict_options remains nullptr.

    auto create_result =
        this->CreateImpl(qb_param, model_instance, final_conflict_options);

    // Clear session's temporary OnConflict if it was used and not overridden by
    // QB's
    if (this->getTempOnConflictClause() && !qb_param.getOnConflictClause() &&
        final_conflict_options == this->getTempOnConflictClause()) {
      this->clearTempOnConflictClause();
    }

    if (!create_result.has_value())
      return std::unexpected(create_result.error());
    // model_instance._is_persisted and afterCreate hook are handled by
    // CreateImpl.

    hook_err = model_instance.afterSave(*this);
    if (hook_err)
      return std::unexpected(hook_err);

    QVariant returned_value_from_create = create_result.value();
    long long rows_affected_or_id = -1;
    if (returned_value_from_create.isValid() &&
        !returned_value_from_create.isNull()) {
      bool ok;
      rows_affected_or_id = returned_value_from_create.toLongLong(&ok);
      if (!ok)
        rows_affected_or_id = -1; // Conversion failed
    }

    // Interpret result for Save's return (usually 1 if successful, 0 if "do
    // nothing" on conflict)
    if (final_conflict_options &&
        final_conflict_options->action == OnConflictClause::Action::DoNothing) {
      return (rows_affected_or_id == 0 && model_instance._is_persisted)
                 ? 0LL
                 : (model_instance._is_persisted
                        ? 1LL
                        : 0LL); // If DO NOTHING and it existed, _is_persisted
                                // should be true, ret 0. If new, ret 1.
    }
    return model_instance._is_persisted ? 1LL
                                        : 0LL; // 1 if persisted (new or updated
                                               // via conflict), 0 otherwise
  }
}

std::expected<long long, Error> Session::Save(ModelBase &model_instance) {
  QueryBuilder qb = this->Model(&model_instance);
  return this->SaveImpl(qb, model_instance);
}

} // namespace cpporm