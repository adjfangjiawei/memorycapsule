#include <QDebug>
#include <QStringList>  // Still used for constructing SQL parts like "(?,?,?)"
#include <QVariant>     // QVariantList for suffix bindings, if QueryBuilder::buildInsertSQLSuffix still uses it

#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_priv_batch_helpers.h"  // 包含声明和 FriendAccess 定义

// SqlDriver specific includes
#include "sqldriver/sql_value.h"

namespace cpporm {
    namespace internal_batch_helpers {

        std::pair<std::vector<ModelBase *>, Error> prepareModelsAndSqlPlaceholders(Session &session,
                                                                                   const std::vector<ModelBase *> &models_in_provider_chunk,
                                                                                   const ModelMeta &meta,
                                                                                   const std::vector<std::string> &batch_ordered_db_field_names_cache,  // IN
                                                                                   BatchSqlParts &out_sql_parts                                         // OUT
        ) {
            std::vector<ModelBase *> models_prepared_for_sql_build;
            models_prepared_for_sql_build.reserve(models_in_provider_chunk.size());
            Error first_prepare_error = make_ok();

            out_sql_parts.all_values_flattened.clear();  // This will now be std::vector<SqlValue>
            out_sql_parts.row_placeholders.clear();      // This is QStringList

            std::string db_driver_name_upper_std;
            if (session.getDbHandle().driver()) {
                std::string drv_name_full = session.getDbHandle().driverName();
                std::transform(drv_name_full.begin(), drv_name_full.end(), std::back_inserter(db_driver_name_upper_std), [](unsigned char c) {
                    return std::toupper(c);
                });
            }

            for (ModelBase *model_ptr : models_in_provider_chunk) {
                if (!model_ptr) continue;

                Error hook_err = model_ptr->beforeCreate(session);
                if (hook_err) {
                    if (first_prepare_error.isOk()) first_prepare_error = hook_err;
                    qWarning() << "prepareModelsAndSqlPlaceholders: beforeCreate hook failed for model (table: " << QString::fromStdString(meta.table_name) << "): " << QString::fromStdString(hook_err.toString());
                    continue;  // Skip this model if hook fails
                }
                FriendAccess::callAutoSetTimestamps(session, *model_ptr, meta, true);

                // extractModelData now returns map<std::string, SqlValue> for fields_to_write
                internal::SessionModelDataForWrite model_data_struct = FriendAccess::callExtractModelData(session, *model_ptr, meta, false, true);

                QStringList current_model_placeholders_segment_qsl;  // For "(?,?,?)" part
                bool model_can_be_inserted_this_pass = false;

                bool is_pure_auto_inc_pk_current_model = batch_ordered_db_field_names_cache.empty() && meta.getPrimaryField() && has_flag(meta.getPrimaryField()->flags, FieldFlag::AutoIncrement);

                if (!batch_ordered_db_field_names_cache.empty()) {
                    model_can_be_inserted_this_pass = true;
                    for (const std::string &field_db_name : batch_ordered_db_field_names_cache) {
                        auto it = model_data_struct.fields_to_write.find(field_db_name);  // Key is std::string
                        // out_sql_parts.all_values_flattened is now std::vector<SqlValue>
                        out_sql_parts.all_values_flattened.push_back(it != model_data_struct.fields_to_write.end() ? it->second : cpporm_sqldriver::SqlValue() /* Null SqlValue */
                        );
                        current_model_placeholders_segment_qsl.append("?");
                    }
                    out_sql_parts.row_placeholders.append(QString("(%1)").arg(current_model_placeholders_segment_qsl.join(",")));
                } else if (is_pure_auto_inc_pk_current_model) {
                    model_can_be_inserted_this_pass = true;
                    // For pure auto-inc PK, SQL syntax (DEFAULT VALUES or empty VALUES ()) depends on DB.
                    // Placeholders for values are not typically used for this case, but the VALUES () or DEFAULT VALUES part is built.
                    // If PostgreSQL and only one such model in the batch, specific "DEFAULT VALUES" SQL is used later.
                    // For multi-row pure auto-inc for PG, or other DBs, often "()" is used per row.
                    if (db_driver_name_upper_std.find("PSQL") == std::string::npos || models_in_provider_chunk.size() > 1) {
                        out_sql_parts.row_placeholders.append("()");  // For MySQL, SQLite, or multi-row PG auto-inc
                    }
                    // For single PG auto-inc, row_placeholders might remain empty, handled by buildFullBatchSqlStatement.
                }

                if (model_can_be_inserted_this_pass) {
                    models_prepared_for_sql_build.push_back(model_ptr);
                } else {
                    if (first_prepare_error.isOk()) {
                        first_prepare_error = Error(ErrorCode::MappingError, "Model (table: " + meta.table_name + ", C++ type: " + typeid(*model_ptr).name() + ") could not be prepared for batch insertion (no insertable fields).");
                    }
                    qWarning() << "prepareModelsAndSqlPlaceholders: Model " << QString::fromStdString(typeid(*model_ptr).name()) << " for table " << QString::fromStdString(meta.table_name)
                               << " could not be prepared for batch insertion (no insertable fields determined or pure-auto-inc logic issue).";
                }
            }
            return {models_prepared_for_sql_build, first_prepare_error};
        }

        Error buildFullBatchSqlStatement(const Session &session,  // const Session& is fine here as we only read db_handle properties
                                         const QueryBuilder &qb_prototype,
                                         const ModelMeta &meta,
                                         const std::vector<std::string> &batch_ordered_db_field_names_cache,  // Already sorted std::string
                                         const OnConflictClause *active_conflict_clause,
                                         BatchSqlParts &in_out_sql_parts  // IN/OUT
        ) {
            in_out_sql_parts.can_proceed = false;
            std::string db_driver_name_upper_std;
            if (session.getDbHandle().driver()) {
                std::string drv_name_full = session.getDbHandle().driverName();
                std::transform(drv_name_full.begin(), drv_name_full.end(), std::back_inserter(db_driver_name_upper_std), [](unsigned char c) {
                    return std::toupper(c);
                });
            }

            std::string sql_verb_std = "INSERT";  // Use std::string
            if (active_conflict_clause && active_conflict_clause->action == OnConflictClause::Action::DoNothing) {
                if (db_driver_name_upper_std.find("MYSQL") != std::string::npos || db_driver_name_upper_std.find("MARIADB") != std::string::npos) {
                    sql_verb_std = "INSERT IGNORE";
                }
                // For SQLite "INSERT OR IGNORE", or PG "ON CONFLICT DO NOTHING", suffix is preferred.
                // If suffix builder for SQLite returns empty for DO NOTHING, then verb needs to change.
                // For now, assume suffix handles it if possible.
            }

            bool is_pure_auto_inc_pk_case = batch_ordered_db_field_names_cache.empty() && meta.getPrimaryField() && has_flag(meta.getPrimaryField()->flags, FieldFlag::AutoIncrement);

            std::string table_name_quoted_std = QueryBuilder::quoteSqlIdentifier(meta.table_name);
            std::ostringstream sql_base_oss;  // Use ostringstream for std::string

            if (is_pure_auto_inc_pk_case) {
                if (db_driver_name_upper_std.find("PSQL") != std::string::npos && in_out_sql_parts.row_placeholders.isEmpty()) {
                    // Single row PG INSERT ... DEFAULT VALUES
                    sql_base_oss << sql_verb_std << " INTO " << table_name_quoted_std << " DEFAULT VALUES";
                    in_out_sql_parts.all_values_flattened.clear();  // No values for DEFAULT VALUES
                } else if (!in_out_sql_parts.row_placeholders.isEmpty()) {
                    // MySQL, SQLite, or multi-row PG: INSERT INTO ... () VALUES (),()...
                    sql_base_oss << sql_verb_std << " INTO " << table_name_quoted_std << " () VALUES " << in_out_sql_parts.row_placeholders.join(",").toStdString();
                    in_out_sql_parts.all_values_flattened.clear();  // No explicit values for "()"
                } else {
                    qWarning() << "buildFullBatchSqlStatement: Inconsistent state for pure auto-inc PK case. Placeholders: " << in_out_sql_parts.row_placeholders.join(",") << ", Driver: " << QString::fromStdString(db_driver_name_upper_std);
                    return Error(ErrorCode::StatementPreparationError, "Pure auto-inc batch SQL build inconsistency.");
                }
            } else if (!batch_ordered_db_field_names_cache.empty() && !in_out_sql_parts.row_placeholders.isEmpty()) {
                sql_base_oss << sql_verb_std << " INTO " << table_name_quoted_std << " (";
                for (size_t i = 0; i < batch_ordered_db_field_names_cache.size(); ++i) {
                    sql_base_oss << QueryBuilder::quoteSqlIdentifier(batch_ordered_db_field_names_cache[i]) << (i < batch_ordered_db_field_names_cache.size() - 1 ? ", " : "");
                }
                sql_base_oss << ") VALUES " << in_out_sql_parts.row_placeholders.join(",").toStdString();
            } else {
                return Error(ErrorCode::StatementPreparationError, "Cannot build batch INSERT SQL: missing field names or placeholders for table " + meta.table_name);
            }
            in_out_sql_parts.sql_insert_base = QString::fromStdString(sql_base_oss.str());

            in_out_sql_parts.sql_on_conflict_suffix.clear();
            in_out_sql_parts.conflict_suffix_bindings.clear();  // This is QVariantList

            if (active_conflict_clause && !(sql_verb_std != "INSERT" && active_conflict_clause->action == OnConflictClause::Action::DoNothing)) {
                QueryBuilder temp_qb_for_suffix_build(nullptr, session.getConnectionName(), &meta);
                temp_qb_for_suffix_build.getState_().on_conflict_clause_ = std::make_unique<OnConflictClause>(*active_conflict_clause);

                auto suffix_pair_result = temp_qb_for_suffix_build.buildInsertSQLSuffix(batch_ordered_db_field_names_cache);
                in_out_sql_parts.sql_on_conflict_suffix = suffix_pair_result.first;     // QString
                in_out_sql_parts.conflict_suffix_bindings = suffix_pair_result.second;  // QVariantList
            }

            in_out_sql_parts.final_sql_statement = in_out_sql_parts.sql_insert_base.toStdString();  // Start with std::string
            if (!in_out_sql_parts.sql_on_conflict_suffix.isEmpty()) {
                in_out_sql_parts.final_sql_statement += " " + in_out_sql_parts.sql_on_conflict_suffix.toStdString();
            }

            // final_bindings is std::vector<SqlValue>
            in_out_sql_parts.final_bindings = in_out_sql_parts.all_values_flattened;  // Already SqlValue vector
            for (const QVariant &qv_suffix_bind : in_out_sql_parts.conflict_suffix_bindings) {
                in_out_sql_parts.final_bindings.push_back(Session::queryValueToSqlValue(QueryBuilder::qvariantToQueryValue(qv_suffix_bind)));
            }

            bool has_pk_for_returning = meta.getPrimaryField() && has_flag(meta.getPrimaryField()->flags, FieldFlag::AutoIncrement) && !meta.getPrimaryField()->db_name.empty();
            bool use_returning = (session.getDbHandle().hasFeature(cpporm_sqldriver::Feature::InsertAndReturnId) && (db_driver_name_upper_std.find("PSQL") != std::string::npos || db_driver_name_upper_std.find("SQLITE") != std::string::npos) && has_pk_for_returning &&
                                  (!active_conflict_clause || active_conflict_clause->action != OnConflictClause::Action::DoNothing));

            if (use_returning) {
                in_out_sql_parts.final_sql_statement += " RETURNING " + QueryBuilder::quoteSqlIdentifier(meta.getPrimaryField()->db_name);
            }

            in_out_sql_parts.can_proceed = true;
            return make_ok();
        }

    }  // namespace internal_batch_helpers
}  // namespace cpporm