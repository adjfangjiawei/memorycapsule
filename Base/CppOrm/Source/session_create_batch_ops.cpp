// Base/CppOrm/Source/session_create_batch_ops.cpp
#include <QDebug>  // For qInfo, qWarning

#include "cpporm/session.h"
// #include <QSqlQuery>   // Removed
#include <QVariant>  // For QVariantList for suffix bindings if buildInsertSQLSuffix still uses it

#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"               // For OnConflictClause in Session, QueryBuilder state access
#include "cpporm/session_priv_batch_helpers.h"  // FriendAccess is defined here

// SqlDriver specific includes
#include "cpporm_sqldriver/sql_database.h"
#include "cpporm_sqldriver/sql_enums.h"  // For Feature::LastInsertId, Feature::InsertAndReturnId
#include "cpporm_sqldriver/sql_query.h"  // For cpporm_sqldriver::SqlQuery
#include "cpporm_sqldriver/sql_value.h"  // For cpporm_sqldriver::SqlValue

namespace cpporm {

    // CreateBatchWithMeta 现在返回 std::expected<size_t, Error>
    std::expected<size_t, Error> Session::CreateBatchWithMeta(const ModelMeta &meta,
                                                              const std::vector<ModelBase *> &models,  // Pointers to models
                                                              size_t internal_batch_processing_size_hint,
                                                              const OnConflictClause *conflict_options_override) {
        if (models.empty()) {
            return 0UL;
        }
        if (meta.table_name.empty()) {
            return std::unexpected(Error(ErrorCode::InvalidConfiguration, "CreateBatchWithMeta: ModelMeta has no table name."));
        }
        if (internal_batch_processing_size_hint == 0) {
            internal_batch_processing_size_hint = 100;            // Default batch size
        } else if (internal_batch_processing_size_hint > 1000) {  // Safety cap
            internal_batch_processing_size_hint = 1000;
        }

        size_t total_successfully_created_count = 0;
        Error first_error_encountered = make_ok();

        QueryBuilder qb_proto = this->Model(meta);  // QB for prototype

        size_t current_provider_idx = 0;
        auto data_provider_lambda = [&]() -> std::optional<std::vector<ModelBase *>> {
            if (current_provider_idx >= models.size()) {
                return std::nullopt;
            }
            std::vector<ModelBase *> chunk;
            size_t end_idx = std::min(models.size(), current_provider_idx + internal_batch_processing_size_hint);
            for (size_t i = current_provider_idx; i < end_idx; ++i) {
                if (models[i]) {  // Ensure pointer is not null
                    chunk.push_back(models[i]);
                }
            }
            current_provider_idx = end_idx;
            if (chunk.empty()) return std::nullopt;
            return chunk;
        };

        auto completion_callback_lambda = [&total_successfully_created_count, &first_error_encountered](const std::vector<ModelBase *> &processed_batch_models_with_ids, Error batch_error) {
            if (batch_error) {
                if (first_error_encountered.isOk()) {
                    first_error_encountered = batch_error;
                }
            } else {
                for (const auto *m_ptr : processed_batch_models_with_ids) {
                    if (m_ptr && m_ptr->_is_persisted) {  // Count only if truly persisted
                        total_successfully_created_count++;
                    }
                }
            }
        };

        Error provider_loop_error = this->CreateBatchProviderInternal(qb_proto, data_provider_lambda, completion_callback_lambda, conflict_options_override);

        if (provider_loop_error) {  // Error from the provider loop itself
            return std::unexpected(provider_loop_error);
        }
        if (first_error_encountered) {  // Error from one of the batch DB operations or hooks
            return std::unexpected(first_error_encountered);
        }

        return total_successfully_created_count;
    }

    Error Session::CreateBatchProviderInternal(QueryBuilder qb_prototype,
                                               std::function<std::optional<std::vector<ModelBase *>>()> data_batch_provider_base,
                                               std::function<void(const std::vector<ModelBase *> &processed_batch_models_with_ids, Error batch_error)> per_db_batch_completion_callback,
                                               const OnConflictClause *conflict_options_override) {
        const ModelMeta *meta_ptr = qb_prototype.getModelMeta();
        if (!meta_ptr) {
            return Error(ErrorCode::InvalidConfiguration, "CreateBatchProviderInternal: QueryBuilder prototype has no ModelMeta.");
        }
        const ModelMeta &meta = *meta_ptr;

        const OnConflictClause *active_conflict_clause = conflict_options_override;
        if (!active_conflict_clause && qb_prototype.getOnConflictClause()) {
            active_conflict_clause = qb_prototype.getOnConflictClause();
        }
        if (!active_conflict_clause && temp_on_conflict_clause_) {
            active_conflict_clause = temp_on_conflict_clause_.get();
        }
        bool clear_temp_clause_at_end = (active_conflict_clause == temp_on_conflict_clause_.get() && !conflict_options_override && !qb_prototype.getOnConflictClause());

        std::vector<std::string> batch_ordered_db_field_names_cache;
        bool is_pure_auto_inc_pk_model_type = data_batch_provider_base && meta.getPrimaryField() && has_flag(meta.getPrimaryField()->flags, FieldFlag::AutoIncrement) && meta.fields.size() == 1 && meta.associations.empty();

        if (!is_pure_auto_inc_pk_model_type) {
            batch_ordered_db_field_names_cache.clear();
            for (const auto &field_meta_obj : meta.fields) {
                if (has_flag(field_meta_obj.flags, FieldFlag::Association)) continue;
                if (has_flag(field_meta_obj.flags, FieldFlag::AutoIncrement) && has_flag(field_meta_obj.flags, FieldFlag::PrimaryKey)) continue;
                if (field_meta_obj.db_name.empty()) continue;
                batch_ordered_db_field_names_cache.push_back(field_meta_obj.db_name);
            }
            std::sort(batch_ordered_db_field_names_cache.begin(), batch_ordered_db_field_names_cache.end());
            if (batch_ordered_db_field_names_cache.empty() && !is_pure_auto_inc_pk_model_type) {
                qWarning() << "CreateBatchProviderInternal: No insertable fields determined for non-pure-auto-inc model" << QString::fromStdString(meta.table_name) << ". This might be valid if DB supports INSERT ... DEFAULT VALUES for such cases.";
            }
        }

        std::optional<std::vector<ModelBase *>> current_batch_models_opt;
        while ((current_batch_models_opt = data_batch_provider_base()).has_value()) {
            if (!current_batch_models_opt.has_value() || current_batch_models_opt->empty()) {
                break;
            }
            std::vector<ModelBase *> &models_in_current_chunk = *current_batch_models_opt;
            if (models_in_current_chunk.empty()) continue;

            internal_batch_helpers::BatchSqlParts sql_parts_for_chunk;
            Error batch_prep_error;
            std::vector<ModelBase *> models_prepared_for_this_db_batch;

            auto prep_result = internal_batch_helpers::prepareModelsAndSqlPlaceholders(*this, models_in_current_chunk, meta, batch_ordered_db_field_names_cache, sql_parts_for_chunk);
            models_prepared_for_this_db_batch = prep_result.first;
            batch_prep_error = prep_result.second;

            if (batch_prep_error) {
                if (per_db_batch_completion_callback) {
                    per_db_batch_completion_callback({}, batch_prep_error);
                }
                continue;
            }
            if (models_prepared_for_this_db_batch.empty()) {
                if (per_db_batch_completion_callback) {
                    per_db_batch_completion_callback({}, make_ok());
                }
                continue;
            }

            Error build_sql_err = internal_batch_helpers::buildFullBatchSqlStatement(*this, qb_prototype, meta, batch_ordered_db_field_names_cache, active_conflict_clause, sql_parts_for_chunk);

            if (build_sql_err || !sql_parts_for_chunk.can_proceed) {
                if (per_db_batch_completion_callback) {
                    per_db_batch_completion_callback({}, build_sql_err.isOk() ? Error(ErrorCode::StatementPreparationError, "SQL construction failed sanity check.") : build_sql_err);
                }
                continue;
            }

            internal_batch_helpers::ExecutionResult exec_res = internal_batch_helpers::executeBatchSql(*this, sql_parts_for_chunk.final_sql_statement, sql_parts_for_chunk.final_bindings, models_prepared_for_this_db_batch, active_conflict_clause);

            std::vector<ModelBase *> successfully_backfilled_models;
            if (!exec_res.db_error) {
                bool driver_has_returning = db_handle_.hasFeature(cpporm_sqldriver::Feature::InsertAndReturnId);
                bool driver_has_last_insert_id = db_handle_.hasFeature(cpporm_sqldriver::Feature::LastInsertId);
                std::string pk_cpp_name_for_backfill;
                std::type_index pk_cpp_type_for_backfill = typeid(void);

                const FieldMeta *pk_field = meta.getPrimaryField();
                if (pk_field && has_flag(pk_field->flags, FieldFlag::AutoIncrement)) {
                    pk_cpp_name_for_backfill = pk_field->cpp_name;
                    pk_cpp_type_for_backfill = pk_field->cpp_type;
                }

                if (!pk_cpp_name_for_backfill.empty() && exec_res.query_object_opt.has_value()) {
                    if (sql_parts_for_chunk.final_sql_statement.find(" RETURNING ") != std::string::npos && driver_has_returning) {
                        successfully_backfilled_models = internal_batch_helpers::backfillIdsFromReturning(exec_res.query_object_opt.value(), meta, exec_res.models_potentially_persisted, pk_cpp_name_for_backfill, pk_cpp_type_for_backfill);
                    } else if (driver_has_last_insert_id) {
                        successfully_backfilled_models = internal_batch_helpers::backfillIdsFromLastInsertId(exec_res.query_object_opt.value(), *this, meta, exec_res.models_potentially_persisted, exec_res.rows_affected, pk_cpp_name_for_backfill, pk_cpp_type_for_backfill, active_conflict_clause);
                    }
                } else {
                    successfully_backfilled_models = exec_res.models_potentially_persisted;
                }

                Error first_hook_error_after_create = make_ok();
                internal_batch_helpers::callAfterCreateHooks(*this, successfully_backfilled_models, first_hook_error_after_create);
                if (first_hook_error_after_create && exec_res.db_error.isOk()) {
                    exec_res.db_error = first_hook_error_after_create;
                }
            }

            if (per_db_batch_completion_callback) {
                per_db_batch_completion_callback(successfully_backfilled_models, exec_res.db_error);
            }
        }

        if (clear_temp_clause_at_end) this->clearTempOnConflictClause();
        return make_ok();
    }

}  // namespace cpporm