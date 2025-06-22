#include <QDateTime>  // For soft delete timestamp
#include <QDebug>     // qWarning, qInfo
#include <QVariant>   // QVariantList from QueryBuilder
#include <algorithm>  // std::min, std::transform

#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm_sqldriver/sql_query.h"  // SqlQuery
#include "cpporm_sqldriver/sql_value.h"  // SqlValue

namespace cpporm {

    std::expected<long long, Error> Session::DeleteImpl(const QueryBuilder &qb_const) {
        QueryBuilder qb = qb_const;  // Work with a copy

        const ModelMeta *meta = qb.getModelMeta();

        if (meta && qb.isSoftDeleteScopeActive()) {
            bool can_soft_delete_this_target = false;
            if (std::holds_alternative<std::string>(qb.getFromClauseSource())) {
                const std::string &from_name = std::get<std::string>(qb.getFromClauseSource());
                if ((!from_name.empty() && from_name == meta->table_name) || (from_name.empty() && !meta->table_name.empty())) {
                    can_soft_delete_this_target = true;
                }
            }

            if (can_soft_delete_this_target) {
                if (const FieldMeta *deletedAtField = meta->findFieldWithFlag(FieldFlag::DeletedAt)) {
                    if (deletedAtField->cpp_type == typeid(QDateTime)) {  // Assuming QDateTime for DeletedAt
                        std::map<std::string, QueryValue> updates_for_soft_delete;
                        updates_for_soft_delete[deletedAtField->db_name] = QDateTime::currentDateTimeUtc();

                        // Also update 'updated_at' if it exists
                        if (const FieldMeta *updatedAtField = meta->findFieldWithFlag(FieldFlag::UpdatedAt)) {
                            if (updatedAtField->cpp_type == typeid(QDateTime)) {
                                updates_for_soft_delete[updatedAtField->db_name] = QDateTime::currentDateTimeUtc();
                            } else {
                                qWarning("Session::DeleteImpl (Soft Delete): Model %s has UpdatedAt field (%s) but it's not QDateTime. It won't be auto-updated during soft delete.", meta->table_name.c_str(), updatedAtField->db_name.c_str());
                            }
                        }
                        // Create a new QueryBuilder for the UPDATE operation, ensuring soft delete scope is off.
                        QueryBuilder update_qb_for_soft_delete = qb;  // Copy original conditions
                        update_qb_for_soft_delete.Unscoped();         // Disable soft delete for the UPDATE itself
                        return this->UpdatesImpl(update_qb_for_soft_delete, updates_for_soft_delete);
                    } else {
                        qWarning("Session::DeleteImpl: Model %s has DeletedAt field (%s) but it's not QDateTime. Soft delete skipped. Hard delete will proceed.", meta->table_name.c_str(), deletedAtField->db_name.c_str());
                    }
                }
            }
        }

        // Proceed with hard delete
        auto [sql_qstr, params_qvariantlist] = qb.buildDeleteSQL();
        std::string sql_std_str = sql_qstr.toStdString();

        if (sql_std_str.empty()) {
            return std::unexpected(Error(ErrorCode::StatementPreparationError, "Failed to build SQL for hard Delete operation."));
        }

        std::vector<cpporm_sqldriver::SqlValue> params_sqlvalue;
        params_sqlvalue.reserve(params_qvariantlist.size());
        for (const QVariant &qv : params_qvariantlist) {
            params_sqlvalue.push_back(Session::queryValueToSqlValue(QueryBuilder::qvariantToQueryValue(qv)));
        }

        auto [sql_query_obj, exec_err] = execute_query_internal(this->db_handle_, sql_std_str, params_sqlvalue);
        if (exec_err) return std::unexpected(exec_err);

        return sql_query_obj.numRowsAffected();
    }

    std::expected<long long, Error> Session::Delete(QueryBuilder qb) {
        if (qb.getExecutor() != this && qb.getExecutor() != nullptr) {
            qWarning(
                "Session::Delete(QueryBuilder): QueryBuilder was associated with "
                "a different executor. The operation will use THIS session's context "
                "by calling its DeleteImpl. Ensure this is intended.");
        }
        return this->DeleteImpl(qb);
    }

    std::expected<long long, Error> Session::Delete(const ModelBase &model_condition) {
        const ModelMeta &meta = model_condition._getOwnModelMeta();
        QueryBuilder qb = this->Model(meta);

        if (meta.primary_keys_db_names.empty()) {
            return std::unexpected(Error(ErrorCode::MappingError, "Delete by model_condition: No PK defined for model " + meta.table_name));
        }

        std::map<std::string, QueryValue> pk_conditions;
        for (const auto &pk_name : meta.primary_keys_db_names) {
            const FieldMeta *fm = meta.findFieldByDbName(pk_name);
            if (!fm) return std::unexpected(Error(ErrorCode::InternalError, "PK field meta not found for DB name '" + pk_name + "' in Delete by model_condition for table " + meta.table_name));
            std::any val = model_condition.getFieldValue(fm->cpp_name);
            if (!val.has_value()) return std::unexpected(Error(ErrorCode::MappingError, "PK value for '" + fm->cpp_name + "' not set in model_condition for Delete on table " + meta.table_name));

            QueryValue qv_pk = Session::anyToQueryValueForSessionConvenience(val);
            if (std::holds_alternative<std::nullptr_t>(qv_pk) && val.has_value()) {
                return std::unexpected(Error(ErrorCode::MappingError, "Delete by model_condition: Unsupported PK type (" + std::string(val.type().name()) + ") for field " + fm->cpp_name));
            }
            pk_conditions[pk_name] = qv_pk;
        }

        if (pk_conditions.empty() || pk_conditions.size() != meta.primary_keys_db_names.size()) return std::unexpected(Error(ErrorCode::MappingError, "Could not extract all PKs for Delete by model_condition on table " + meta.table_name));

        qb.Where(pk_conditions);
        return this->DeleteImpl(qb);
    }

    std::expected<long long, Error> Session::Delete(const ModelMeta &meta, const std::map<std::string, QueryValue> &conditions) {
        QueryBuilder qb = this->Model(meta);
        if (!conditions.empty()) {
            qb.Where(conditions);
        }  // If conditions are empty, buildDeleteSQL will warn about missing WHERE if applicable
        return this->DeleteImpl(qb);
    }

    std::expected<long long, Error> Session::DeleteBatch(const ModelMeta &meta, const std::vector<std::map<std::string, QueryValue>> &primary_keys_list, size_t batch_delete_size_hint) {
        if (primary_keys_list.empty()) {
            return 0LL;
        }
        if (meta.table_name.empty()) {
            return std::unexpected(Error(ErrorCode::InvalidConfiguration, "DeleteBatch: ModelMeta does not have a valid table name."));
        }
        if (meta.primary_keys_db_names.empty()) {
            return std::unexpected(Error(ErrorCode::MappingError, "DeleteBatch: Model " + meta.table_name + " has no primary keys defined."));
        }

        long long total_rows_affected_accumulator = 0;
        Error first_error_encountered = make_ok();
        bool an_error_occurred_in_any_batch = false;

        size_t actual_batch_size = batch_delete_size_hint > 0 ? batch_delete_size_hint : 100;  // Default to 100 if 0
        if (primary_keys_list.empty())
            actual_batch_size = 0;  // No batches if list is empty
        else if (actual_batch_size == 0)
            actual_batch_size = 1;  // Ensure at least 1 if list not empty and hint was 0

        if (actual_batch_size > 500) actual_batch_size = 500;  // Cap batch size for safety

        for (size_t i = 0; i < primary_keys_list.size(); i += actual_batch_size) {
            QueryBuilder qb_for_this_batch(this, this->connection_name_, &meta);

            size_t current_batch_end_idx = std::min(i + actual_batch_size, primary_keys_list.size());
            if (current_batch_end_idx <= i) continue;  // Should not happen with proper loop

            if (meta.primary_keys_db_names.size() == 1) {  // Single PK
                const std::string &pk_col_db_name = meta.primary_keys_db_names[0];
                std::vector<QueryValue> pk_values_for_in_clause;
                pk_values_for_in_clause.reserve(current_batch_end_idx - i);

                for (size_t k = i; k < current_batch_end_idx; ++k) {
                    const auto &pk_map_for_item = primary_keys_list[k];
                    auto it = pk_map_for_item.find(pk_col_db_name);
                    if (it != pk_map_for_item.end()) {
                        pk_values_for_in_clause.push_back(it->second);
                    } else {
                        qWarning("DeleteBatch: PK '%s' not found in map for item at index %zu. Skipping this item.", pk_col_db_name.c_str(), k);
                    }
                }
                if (!pk_values_for_in_clause.empty()) {
                    qb_for_this_batch.In(pk_col_db_name, pk_values_for_in_clause);
                } else {
                    continue;  // No valid PKs in this sub-batch
                }
            } else {  // Composite PKs
                std::vector<std::string> or_conditions_str_parts;
                std::vector<QueryValue> all_composite_pk_bindings;
                or_conditions_str_parts.reserve(current_batch_end_idx - i);

                for (size_t k = i; k < current_batch_end_idx; ++k) {
                    std::string current_item_pk_condition_group_str = "(";
                    bool first_col_in_group = true;
                    bool current_item_pk_group_valid = true;
                    std::vector<QueryValue> bindings_for_current_item_group;
                    bindings_for_current_item_group.reserve(meta.primary_keys_db_names.size());

                    for (const std::string &pk_col_db_name_part : meta.primary_keys_db_names) {
                        const auto &pk_map_for_item = primary_keys_list[k];
                        auto it = pk_map_for_item.find(pk_col_db_name_part);
                        if (it == pk_map_for_item.end()) {
                            qWarning("DeleteBatch: Composite PK part '%s' not found for item at index %zu. Skipping this item.", pk_col_db_name_part.c_str(), k);
                            current_item_pk_group_valid = false;
                            break;
                        }
                        if (!first_col_in_group) {
                            current_item_pk_condition_group_str += " AND ";
                        }
                        current_item_pk_condition_group_str += QueryBuilder::quoteSqlIdentifier(pk_col_db_name_part) + " = ?";
                        bindings_for_current_item_group.push_back(it->second);
                        first_col_in_group = false;
                    }
                    current_item_pk_condition_group_str += ")";

                    if (current_item_pk_group_valid && !bindings_for_current_item_group.empty()) {
                        or_conditions_str_parts.push_back(current_item_pk_condition_group_str);
                        all_composite_pk_bindings.insert(all_composite_pk_bindings.end(), std::make_move_iterator(bindings_for_current_item_group.begin()), std::make_move_iterator(bindings_for_current_item_group.end()));
                    }
                }
                if (!or_conditions_str_parts.empty()) {
                    std::string final_or_where_clause_str;
                    for (size_t o_idx = 0; o_idx < or_conditions_str_parts.size(); ++o_idx) {
                        final_or_where_clause_str += or_conditions_str_parts[o_idx];
                        if (o_idx < or_conditions_str_parts.size() - 1) {
                            final_or_where_clause_str += " OR ";
                        }
                    }
                    qb_for_this_batch.Where(final_or_where_clause_str, all_composite_pk_bindings);
                } else {
                    continue;  // No valid composite PKs in this sub-batch
                }
            }

            auto batch_delete_result = this->DeleteImpl(qb_for_this_batch);

            if (batch_delete_result.has_value()) {
                total_rows_affected_accumulator += batch_delete_result.value();
            } else {
                if (!an_error_occurred_in_any_batch) {
                    first_error_encountered = batch_delete_result.error();
                }
                an_error_occurred_in_any_batch = true;  // Mark that an error occurred
                qWarning("DeleteBatch: Error in sub-batch for table %s. Error: %s", meta.table_name.c_str(), batch_delete_result.error().toString().c_str());
                // Optionally break or continue processing other batches
            }
        }

        if (an_error_occurred_in_any_batch) return std::unexpected(first_error_encountered);
        return total_rows_affected_accumulator;
    }

}  // namespace cpporm