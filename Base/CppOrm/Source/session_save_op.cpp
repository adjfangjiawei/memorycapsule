#include <QDateTime>  // For timestamp logic, QVariant in QueryValue
#include <QDebug>     // qWarning, qInfo
#include <QMetaType>  // For QVariant -> QueryValue helper
#include <QVariant>   // QueryValue helper

#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "sqldriver/sql_value.h"  // SqlValue

namespace cpporm {

    std::expected<long long, Error> Session::SaveImpl(const QueryBuilder &qb_param, ModelBase &model_instance) {
        const ModelMeta *meta_from_qb = qb_param.getModelMeta();
        const ModelMeta &meta = meta_from_qb ? *meta_from_qb : model_instance._getOwnModelMeta();

        if (meta.table_name.empty()) {
            return std::unexpected(Error(ErrorCode::InvalidConfiguration, "SaveImpl: ModelMeta does not have a valid table_name."));
        }

        Error hook_err = model_instance.beforeSave(*this);
        if (hook_err) return std::unexpected(hook_err);

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
                    // Check for "zero" or "empty" values more robustly
                    QueryValue pk_qv = Session::anyToQueryValueForSessionConvenience(pk_val_any);
                    if (std::holds_alternative<std::nullptr_t>(pk_qv)) {  // Covers uninitialized or unconvertible types
                        model_has_all_pks_set_and_non_default = false;
                        break;
                    }
                    if (std::holds_alternative<int>(pk_qv) && std::get<int>(pk_qv) == 0) {
                        model_has_all_pks_set_and_non_default = false;
                        break;
                    }
                    if (std::holds_alternative<long long>(pk_qv) && std::get<long long>(pk_qv) == 0) {
                        model_has_all_pks_set_and_non_default = false;
                        break;
                    }
                    if (std::holds_alternative<std::string>(pk_qv) && std::get<std::string>(pk_qv).empty()) {
                        model_has_all_pks_set_and_non_default = false;
                        break;
                    }
                    // Add checks for other PK types if necessary (e.g., QDate for 0000-00-00)
                } else {
                    model_has_all_pks_set_and_non_default = false;
                    qWarning() << "SaveImpl: PK field meta not found for" << QString::fromStdString(pk_db_name);
                    break;
                }
            }
        }

        bool attempt_update = (model_instance._is_persisted || model_has_all_pks_set_and_non_default) && has_defined_pk;

        if (attempt_update) {
            this->autoSetTimestamps(model_instance, meta, false);                                                         // false for update
            internal::SessionModelDataForWrite data_to_write = this->extractModelData(model_instance, meta, true, true);  // true for update, true for include_timestamps

            if (data_to_write.primary_key_fields.empty() && has_defined_pk) {
                return std::unexpected(Error(ErrorCode::MappingError, "SaveImpl (Update path): Failed to extract valid primary key values for WHERE clause. Table: " + meta.table_name));
            }

            if (data_to_write.fields_to_write.empty()) {
                qInfo("SaveImpl (Update path): No fields (including timestamps) to update for table %s. Skipping DB operation.", meta.table_name.c_str());
                // Still run hooks if appropriate for a "no-op save that was an update attempt"
                hook_err = model_instance.beforeUpdate(*this);
                if (hook_err) return std::unexpected(hook_err);
                hook_err = model_instance.afterUpdate(*this);
                if (hook_err) return std::unexpected(hook_err);
                hook_err = model_instance.afterSave(*this);
                if (hook_err) return std::unexpected(hook_err);
                return 0LL;  // 0 rows affected
            }

            hook_err = model_instance.beforeUpdate(*this);
            if (hook_err) return std::unexpected(hook_err);

            QueryBuilder update_qb(this, this->connection_name_, &meta);
            for (const auto &pk_name_std : meta.primary_keys_db_names) {
                auto it = data_to_write.primary_key_fields.find(pk_name_std);
                if (it != data_to_write.primary_key_fields.end() && it->second.isValid() && !it->second.isNull()) {
                    update_qb.Where(pk_name_std + " = ?", {Session::sqlValueToQueryValue(it->second)});
                } else {
                    return std::unexpected(Error(ErrorCode::MappingError, "SaveImpl (Update path): PK '" + pk_name_std + "' missing or invalid in extracted PKs for WHERE clause. Table: " + meta.table_name));
                }
            }

            std::map<std::string, QueryValue> updates_for_impl;
            for (const auto &pair : data_to_write.fields_to_write) {
                // Do not include PKs in the SET clause of an UPDATE
                bool is_this_field_a_pk = false;
                for (const auto &pk_col_name : meta.primary_keys_db_names) {
                    if (pk_col_name == pair.first) {
                        is_this_field_a_pk = true;
                        break;
                    }
                }
                if (!is_this_field_a_pk) {
                    updates_for_impl[pair.first] = Session::sqlValueToQueryValue(pair.second);
                }
            }

            if (updates_for_impl.empty()) {  // If only PKs were in fields_to_write (unlikely due to timestamp logic)
                qInfo("SaveImpl (Update path): After removing PKs, no fields left to update for table %s. Skipping DB operation.", meta.table_name.c_str());
                hook_err = model_instance.afterUpdate(*this);
                if (hook_err) return std::unexpected(hook_err);
                hook_err = model_instance.afterSave(*this);
                if (hook_err) return std::unexpected(hook_err);
                return 0LL;
            }

            auto update_result = this->UpdatesImpl(update_qb, updates_for_impl);

            if (!update_result.has_value()) return std::unexpected(update_result.error());
            if (update_result.value() > 0)
                model_instance._is_persisted = true;  // If rows were affected, it's persisted
            else if (update_result.value() == 0 && model_instance._is_persisted) {
                // No rows affected, but it was already persisted (e.g., no actual data change)
            } else if (update_result.value() == 0 && !model_instance._is_persisted && model_has_all_pks_set_and_non_default) {
                // PKs were set, but no matching row found for update. This means it's not persisted.
                // This Save should have gone to Create path.
                // This case indicates a potential issue if we expected an update.
                // GORM might then attempt an INSERT. For now, we don't auto-switch.
                qWarning("SaveImpl (Update path): Update affected 0 rows for model (table: %s) with PKs set but not previously marked persisted. Record may not exist.", meta.table_name.c_str());
            }

            hook_err = model_instance.afterUpdate(*this);
            if (hook_err) return std::unexpected(hook_err);
            hook_err = model_instance.afterSave(*this);
            if (hook_err) return std::unexpected(hook_err);
            return update_result.value();

        } else {  // Attempt a CREATE
            // hook_err = model_instance.beforeCreate(*this); // CreateImpl handles this
            // if (hook_err) return std::unexpected(hook_err);
            // this->autoSetTimestamps(model_instance, meta, true); // CreateImpl handles this

            const OnConflictClause *final_conflict_options = nullptr;
            std::unique_ptr<OnConflictClause> save_upsert_clause_ptr;

            if (qb_param.getOnConflictClause()) {
                final_conflict_options = qb_param.getOnConflictClause();
            } else if (this->getTempOnConflictClause()) {
                final_conflict_options = this->getTempOnConflictClause();
            } else if (has_defined_pk && model_has_all_pks_set_and_non_default) {
                // If PKs are set and it's not persisted, default to upsert (update all excluded)
                save_upsert_clause_ptr = std::make_unique<OnConflictClause>(OnConflictClause::Action::UpdateAllExcluded);
                // Set conflict target to PKs for databases like PostgreSQL/SQLite
                if (!meta.primary_keys_db_names.empty() && save_upsert_clause_ptr->conflict_target_columns_db_names.empty()) {
                    save_upsert_clause_ptr->conflict_target_columns_db_names = meta.primary_keys_db_names;
                }
                final_conflict_options = save_upsert_clause_ptr.get();
            }

            auto create_result_sv_expected = this->CreateImpl(qb_param, model_instance, final_conflict_options);

            if (this->getTempOnConflictClause() && !qb_param.getOnConflictClause() && final_conflict_options == this->getTempOnConflictClause()) {
                this->clearTempOnConflictClause();
            }

            if (!create_result_sv_expected.has_value()) return std::unexpected(create_result_sv_expected.error());

            // model_instance._is_persisted and afterCreate hook are handled by CreateImpl

            hook_err = model_instance.afterSave(*this);  // afterSave is specific to Save operation
            if (hook_err) return std::unexpected(hook_err);

            // Determine return value for Save (usually 1 for create/update, 0 for no-op)
            // create_result_sv_expected.value() contains rows_affected or last_insert_id from CreateImpl
            cpporm_sqldriver::SqlValue sv_from_create = create_result_sv_expected.value();
            long long rows_affected_from_create = -1;
            bool ok_conv = false;
            if (sv_from_create.type() == cpporm_sqldriver::SqlValueType::Int64) {  // If it was rows_affected
                rows_affected_from_create = sv_from_create.toInt64(&ok_conv);
            }

            if (final_conflict_options && final_conflict_options->action == OnConflictClause::Action::DoNothing) {
                // If DO NOTHING occurred, and CreateImpl reported 0 rows affected (meaning it existed),
                // _is_persisted should reflect that (CreateImpl should set it true if conflict handler ran).
                // Save operation should return 0 if it was a DO NOTHING on existing.
                // If it was a genuine insert (conflict didn't happen), rows_affected would be > 0.
                if (ok_conv && rows_affected_from_create == 0 && model_instance._is_persisted) return 0LL;  // No-op due to conflict
                if (model_instance._is_persisted) return 1LL;                                               // Actual insert or update from conflict
                return 0LL;                                                                                 // Fallback if not persisted after DO NOTHING attempt
            }
            return model_instance._is_persisted ? 1LL : 0LL;  // 1 if persisted (newly or via upsert), 0 otherwise
        }
    }

    std::expected<long long, Error> Session::Save(ModelBase &model_instance) {
        QueryBuilder qb = this->Model(&model_instance);
        return this->SaveImpl(qb, model_instance);
    }

}  // namespace cpporm