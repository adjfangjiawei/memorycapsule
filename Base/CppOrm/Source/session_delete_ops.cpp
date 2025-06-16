// cpporm/session_delete_ops.cpp
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h" // Now includes core, execution, and state
#include "cpporm/session.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm> // for std::min

namespace cpporm {

// Session 的 IQueryExecutor::DeleteImpl 实现
std::expected<long long, Error>
Session::DeleteImpl(const QueryBuilder &qb_const) {
  QueryBuilder qb = qb_const; // Work with a copy

  const ModelMeta *meta = qb.getModelMeta();

  // Soft Delete Logic
  if (meta && qb.isSoftDeleteScopeActive()) { // isSoftDeleteScopeActive is from
                                              // QueryBuilderScopesMixin
    if (const FieldMeta *deletedAtField =
            meta->findFieldWithFlag(FieldFlag::DeletedAt)) {

      bool can_soft_delete_this_target = false;
      if (std::holds_alternative<std::string>(qb.getFromClauseSource())) {
        const std::string &from_name =
            std::get<std::string>(qb.getFromClauseSource());
        // Check if the FROM source is the model's primary table
        if ((!from_name.empty() && from_name == meta->table_name) ||
            (from_name.empty() &&
             !meta->table_name
                  .empty() /* implies default to model's table */)) {
          can_soft_delete_this_target = true;
        }
      }

      if (can_soft_delete_this_target) {
        if (deletedAtField->cpp_type == typeid(QDateTime)) {
          std::map<std::string, QueryValue> updates_for_soft_delete;
          updates_for_soft_delete[deletedAtField->db_name] =
              QDateTime::currentDateTimeUtc();

          if (const FieldMeta *updatedAtField =
                  meta->findFieldWithFlag(FieldFlag::UpdatedAt)) {
            if (updatedAtField->cpp_type == typeid(QDateTime)) {
              updates_for_soft_delete[updatedAtField->db_name] =
                  QDateTime::currentDateTimeUtc();
            } else {
              qWarning("Session::DeleteImpl (Soft Delete): Model %s has "
                       "UpdatedAt field (%s) but it's not QDateTime. It won't "
                       "be auto-updated during soft delete.",
                       meta->table_name.c_str(),
                       updatedAtField->db_name.c_str());
            }
          }

          QueryBuilder update_qb_for_soft_delete = qb;
          update_qb_for_soft_delete.Unscoped();

          // Call UpdatesImpl which correctly builds UPDATE SQL and handles
          // hooks UpdatesImpl (which is Session::UpdatesImpl) will handle its
          // own timestamp logic based on the qb and meta. If we set updated_at
          // here, it's fine.
          return this->UpdatesImpl(update_qb_for_soft_delete,
                                   updates_for_soft_delete);
        } else {
          qWarning(
              "Session::DeleteImpl: Model %s has DeletedAt field (%s) but it's "
              "not QDateTime. Soft delete skipped. Hard delete will proceed.",
              meta->table_name.c_str(), deletedAtField->db_name.c_str());
        }
      }
    }
  }

  // If not soft-deleted, proceed with hard delete
  auto [sql, params] = qb.buildDeleteSQL();
  if (sql.isEmpty()) {
    return std::unexpected(
        Error(ErrorCode::StatementPreparationError,
              "Failed to build SQL for hard Delete operation."));
  }

  // Hooks are generally managed by higher-level methods if model instances are
  // involved. DeleteImpl is generic.

  auto [query_obj, exec_err] =
      execute_query_internal(this->db_handle_, sql, params);
  if (exec_err)
    return std::unexpected(exec_err);

  return query_obj.numRowsAffected();
}

// Session 的便捷 Delete 方法 (通过 QB)
std::expected<long long, Error> Session::Delete(QueryBuilder qb) {
  if (qb.getExecutor() != this && qb.getExecutor() != nullptr) {
    qWarning("Session::Delete(QueryBuilder): QueryBuilder was associated with "
             "a different executor. "
             "The operation will use THIS session's context by calling its "
             "DeleteImpl. "
             "Ensure this is intended.");
  }
  // Always use this session's Impl method for consistency when
  // Session::Delete(QB) is called.
  return this->DeleteImpl(qb);
}

// Session 的便捷 Delete 方法 (通过 ModelBase 实例)
std::expected<long long, Error>
Session::Delete(const ModelBase &model_condition) {
  const ModelMeta &meta = model_condition._getOwnModelMeta();
  // Use session's Model<T>() or Model(meta) to get a QB associated with this
  // session
  QueryBuilder qb = this->Model(meta);

  if (meta.primary_keys_db_names.empty()) {
    return std::unexpected(
        Error(ErrorCode::MappingError,
              "Delete by model_condition: No PK defined for model " +
                  meta.table_name));
  }

  // Hooks: GORM typically calls hooks on the model instance if Delete(&model)
  // is used. For Delete(ModelBase&), we might need to cast away const to call
  // non-const hooks. This is a design choice. For now, assuming hooks handled
  // at a higher level or if a non-const ModelBase& was passed. If we want to
  // call hooks here: cpporm::Error hook_err =
  // const_cast<ModelBase&>(model_condition).beforeDelete(*this); if(hook_err)
  // return std::unexpected(hook_err);

  std::map<std::string, QueryValue> pk_conditions;
  for (const auto &pk_name : meta.primary_keys_db_names) {
    const FieldMeta *fm = meta.findFieldByDbName(pk_name);
    if (!fm)
      return std::unexpected(Error(
          ErrorCode::InternalError,
          "PK field meta not found for DB name '" + pk_name +
              "' in Delete by model_condition for table " + meta.table_name));
    std::any val = model_condition.getFieldValue(fm->cpp_name);
    if (!val.has_value())
      return std::unexpected(
          Error(ErrorCode::MappingError,
                "PK value for '" + fm->cpp_name +
                    "' not set in model_condition for Delete on table " +
                    meta.table_name));

    QueryValue qv_pk = Session::anyToQueryValueForSessionConvenience(val);
    if (std::holds_alternative<std::nullptr_t>(qv_pk) &&
        val.has_value()) { // Conversion failed
      return std::unexpected(Error(
          ErrorCode::MappingError,
          "Delete by model_condition: Unsupported PK type (" +
              std::string(val.type().name()) + ") for field " + fm->cpp_name));
    }
    pk_conditions[pk_name] = qv_pk;
  }

  if (pk_conditions.empty() ||
      pk_conditions.size() !=
          meta.primary_keys_db_names
              .size()) // Ensure all PKs were found and valid
    return std::unexpected(Error(
        ErrorCode::MappingError,
        "Could not extract all PKs for Delete by model_condition on table " +
            meta.table_name));

  qb.Where(pk_conditions);

  auto delete_result = this->DeleteImpl(qb);

  // if (delete_result.has_value() && delete_result.value() > 0) {
  //   cpporm::Error hook_err_after =
  //   const_cast<ModelBase&>(model_condition).afterDelete(*this);
  //   if(hook_err_after) return std::unexpected(hook_err_after);
  // }
  return delete_result;
}

// Session 的便捷 Delete 方法 (通过 ModelMeta 和条件)
std::expected<long long, Error>
Session::Delete(const ModelMeta &meta,
                const std::map<std::string, QueryValue> &conditions) {
  QueryBuilder qb = this->Model(meta);
  if (!conditions.empty()) {
    qb.Where(conditions);
  } else {
    // Warning about deleting all rows is handled by
    // QueryBuilder::buildDeleteSQL if no WHERE clause is ultimately produced.
  }
  return this->DeleteImpl(qb);
}

std::expected<long long, Error> Session::DeleteBatch(
    const ModelMeta &meta,
    const std::vector<std::map<std::string, QueryValue>> &primary_keys_list,
    size_t batch_delete_size_hint) {

  if (primary_keys_list.empty()) {
    return 0LL;
  }
  if (meta.table_name.empty()) {
    return std::unexpected(
        Error(ErrorCode::InvalidConfiguration,
              "DeleteBatch: ModelMeta does not have a valid table name."));
  }
  if (meta.primary_keys_db_names.empty()) {
    return std::unexpected(
        Error(ErrorCode::MappingError, "DeleteBatch: Model " + meta.table_name +
                                           " has no primary keys defined."));
  }

  long long total_rows_affected_accumulator = 0;
  Error first_error_encountered = make_ok();
  bool an_error_occurred_in_any_batch = false;

  size_t actual_batch_size = batch_delete_size_hint;
  if (actual_batch_size == 0 && !primary_keys_list.empty())
    actual_batch_size = 1;
  if (actual_batch_size > 200)
    actual_batch_size = 100; // Cap batch size

  for (size_t i = 0; i < primary_keys_list.size(); i += actual_batch_size) {
    // Create a QB specifically for this session and meta for each batch
    QueryBuilder qb_for_this_batch(this, this->connection_name_, &meta);

    size_t current_batch_end_idx =
        std::min(i + actual_batch_size, primary_keys_list.size());

    if (current_batch_end_idx <= i)
      continue;

    if (meta.primary_keys_db_names.size() == 1) { // Single PK
      const std::string &pk_col_db_name = meta.primary_keys_db_names[0];
      std::vector<QueryValue> pk_values_for_in_clause;
      pk_values_for_in_clause.reserve(current_batch_end_idx - i);

      for (size_t k = i; k < current_batch_end_idx; ++k) {
        const auto &pk_map_for_item = primary_keys_list[k];
        auto it = pk_map_for_item.find(pk_col_db_name);
        if (it != pk_map_for_item.end()) {
          pk_values_for_in_clause.push_back(it->second);
        } else {
          qWarning("DeleteBatch: PK '%s' not found in map for item at index "
                   "%zu. Skipping this item.",
                   pk_col_db_name.c_str(), k);
        }
      }
      if (!pk_values_for_in_clause.empty()) {
        std::string placeholders_for_in;
        for (size_t p = 0; p < pk_values_for_in_clause.size(); ++p) {
          placeholders_for_in += (p == 0 ? "?" : ",?");
        }
        qb_for_this_batch.Where(
            QueryBuilder::quoteSqlIdentifier(pk_col_db_name) + " IN (" +
                placeholders_for_in + ")",
            pk_values_for_in_clause);
      } else {
        continue;
      }
    } else { // Composite PKs
      std::vector<std::string> or_conditions_str_parts;
      std::vector<QueryValue> all_composite_pk_bindings;
      or_conditions_str_parts.reserve(current_batch_end_idx - i);

      for (size_t k = i; k < current_batch_end_idx; ++k) {
        std::string current_item_pk_condition_group_str = "(";
        bool first_col_in_group = true;
        bool current_item_pk_group_valid = true;
        std::vector<QueryValue> bindings_for_current_item_group;
        bindings_for_current_item_group.reserve(
            meta.primary_keys_db_names.size());

        for (const std::string &pk_col_db_name_part :
             meta.primary_keys_db_names) {
          const auto &pk_map_for_item = primary_keys_list[k];
          auto it = pk_map_for_item.find(pk_col_db_name_part);
          if (it == pk_map_for_item.end()) {
            qWarning("DeleteBatch: Composite PK part '%s' not found for item "
                     "at index %zu. Skipping this item.",
                     pk_col_db_name_part.c_str(), k);
            current_item_pk_group_valid = false;
            break;
          }
          if (!first_col_in_group) {
            current_item_pk_condition_group_str += " AND ";
          }
          current_item_pk_condition_group_str +=
              QueryBuilder::quoteSqlIdentifier(pk_col_db_name_part) + " = ?";
          bindings_for_current_item_group.push_back(it->second);
          first_col_in_group = false;
        }
        current_item_pk_condition_group_str += ")";

        if (current_item_pk_group_valid &&
            !bindings_for_current_item_group.empty()) {
          or_conditions_str_parts.push_back(
              current_item_pk_condition_group_str);
          all_composite_pk_bindings.insert(
              all_composite_pk_bindings.end(),
              std::make_move_iterator(bindings_for_current_item_group.begin()),
              std::make_move_iterator(bindings_for_current_item_group.end()));
        }
      }
      if (!or_conditions_str_parts.empty()) {
        std::string final_or_where_clause_str;
        for (size_t o_idx = 0; o_idx < or_conditions_str_parts.size();
             ++o_idx) {
          final_or_where_clause_str += or_conditions_str_parts[o_idx];
          if (o_idx < or_conditions_str_parts.size() - 1) {
            final_or_where_clause_str += " OR ";
          }
        }
        qb_for_this_batch.Where(final_or_where_clause_str,
                                all_composite_pk_bindings);
      } else {
        continue;
      }
    }

    auto batch_delete_result = this->DeleteImpl(qb_for_this_batch);

    if (batch_delete_result.has_value()) {
      total_rows_affected_accumulator += batch_delete_result.value();
    } else {
      if (!an_error_occurred_in_any_batch) {
        first_error_encountered = batch_delete_result.error();
        an_error_occurred_in_any_batch = true;
      }
      qWarning("DeleteBatch: Error in sub-batch for table %s. Error: %s",
               meta.table_name.c_str(),
               batch_delete_result.error().toString().c_str());
    }
  }

  if (an_error_occurred_in_any_batch)
    return std::unexpected(first_error_encountered);
  return total_rows_affected_accumulator;
}

} // namespace cpporm