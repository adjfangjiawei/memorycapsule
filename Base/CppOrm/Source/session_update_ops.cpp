// cpporm/session_update_ops.cpp
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlQuery>
#include <QVariant>

namespace cpporm {

// Session 的 IQueryExecutor::UpdatesImpl 实现
std::expected<long long, Error>
Session::UpdatesImpl(const QueryBuilder &qb_const, // Renamed to avoid conflict
                     const std::map<std::string, QueryValue> &updates_map_in) {

  QueryBuilder qb =
      qb_const; // Work with a copy to modify (e.g., add timestamps)

  if (updates_map_in.empty()) {
    qInfo("cpporm Session::UpdatesImpl: No update values provided.");
    return 0LL;
  }

  std::map<std::string, QueryValue> final_updates = updates_map_in;
  const ModelMeta *meta = qb.getModelMeta();

  // Automatically add 'updated_at' if the model has it
  if (meta) {
    // Check if the FROM source is actually the model's table before blindly
    // adding timestamps
    bool update_model_table_directly = false;
    if (std::holds_alternative<std::string>(qb.getFromClauseSource())) {
      const std::string &from_name =
          std::get<std::string>(qb.getFromClauseSource());
      if (!from_name.empty() && from_name == meta->table_name) {
        update_model_table_directly = true;
      } else if (from_name.empty() &&
                 !meta->table_name.empty()) { // Defaulted to model table
        update_model_table_directly = true;
      }
    }

    if (update_model_table_directly) {
      if (const FieldMeta *updatedAtField =
              meta->findFieldWithFlag(FieldFlag::UpdatedAt)) {
        if (updatedAtField->cpp_type == typeid(QDateTime)) {
          // Add or overwrite updated_at in the final_updates map
          final_updates[updatedAtField->db_name] =
              QDateTime::currentDateTimeUtc();
        }
      }
    }
  }

  auto [sql, params] = qb.buildUpdateSQL(final_updates); // qb is already a copy
  if (sql.isEmpty()) {
    return std::unexpected(
        Error(ErrorCode::StatementPreparationError,
              "Failed to build SQL for Updates operation. Target might be "
              "invalid (e.g., a subquery) or table name missing."));
  }

  auto [query_obj, exec_err] =
      execute_query_internal(this->db_handle_, sql, params);
  if (exec_err) {
    return std::unexpected(exec_err);
  }

  return query_obj.numRowsAffected();
}

// Session 便捷方法 Updates(QueryBuilder, map)
std::expected<long long, Error>
Session::Updates(QueryBuilder qb, // Pass QB by value to allow modification if
                                  // needed by QB's own Updates
                 const std::map<std::string, QueryValue> &updates) {
  if (qb.getExecutor() == nullptr) {
    // This typically means QueryBuilder was not created via a Session.
    // We need to ensure it uses *this* session's executor.
    // However, QueryBuilder::Updates itself calls its stored executor.
    // This design is a bit tricky. For now, assume the user intends for
    // the QB to be executed by *this* session if they call Session::Updates.
    // A safer design might involve QueryBuilder not storing an executor,
    // and Session always providing it.
    // For this specific call path, since it's Session::Updates, we will use
    // *this* session's Impl.
    QueryBuilder qb_with_this_session_executor = qb; // Make a copy
    // qb_with_this_session_executor.executor_ = this; // This is private, can't
    // do.
    //  The `qb.Updates()` call will use its own executor.
    //  The correct way for Session::Updates(QB, map) is to call *this*
    //  session's Impl. We use the passed qb's state but execute with *this*
    //  session.

    // If the QB was created by a *different* session, this is problematic.
    // If it was created by *this* session, qb.getExecutor() == this, so
    // qb.Updates() is fine. If it was default-constructed (executor=nullptr),
    // then qb.Updates() will fail.

    // Simplest approach: if qb's executor is not this, warn and proceed using
    // qb's executor (via its own .Updates()) OR, reconstruct a QB for this
    // session using the state of the passed QB. (Complex) OR, the
    // Session::Updates(QB,map) should not exist, and users should always call
    // qb.Updates() directly.

    // Let's assume if Session::Updates(QB, map) is called, the QB should be
    // executed by *this* session. The most straightforward is to call the Impl
    // method of *this* session, passing the qb. QueryBuilder::Updates is just a
    // public wrapper around executor_->UpdatesImpl(*this, updates) So, calling
    // this->UpdatesImpl(qb, updates) makes sense.
    if (qb.getExecutor() != this && qb.getExecutor() != nullptr) {
      qWarning(
          "Session::Updates(QueryBuilder, ...): QueryBuilder was associated "
          "with a different executor. The operation will use THIS session's "
          "context, "
          "but the QueryBuilder's state. Ensure this is intended.");
    }
    return this->UpdatesImpl(qb, updates);
  }
  // If qb.getExecutor() == this, then qb.Updates() is fine.
  return qb.Updates(updates);
}

// Session 便捷方法 Updates(const ModelMeta&, updates_map, conditions_map)
std::expected<long long, Error> Session::Updates(
    const ModelMeta &meta,
    const std::map<std::string, QueryValue> &updates_map, // Renamed
    const std::map<std::string, QueryValue> &conditions) {
  if (updates_map.empty()) {
    qInfo("cpporm Session::Updates (by meta): No update values provided.");
    return 0LL;
  }
  QueryBuilder qb = this->Model(meta);
  if (!conditions.empty()) {
    qb.Where(conditions);
  } else {
    // This warning is good, but buildUpdateSQL also has a similar one if no
    // WHERE clause is produced. qWarning("cpporm Session::Updates (by meta):
    // Attempting to update table "
    //          "'%s' without WHERE conditions. This will update ALL rows.",
    //          meta.table_name.c_str());
  }
  // Call the Impl method, not qb.Updates() to ensure correct executor and
  // timestamp logic
  return this->UpdatesImpl(qb, updates_map);
}

// Session 便捷方法 Updates(const ModelBase& model_condition, updates_map)
std::expected<long long, Error> Session::Updates(
    const ModelBase &model_condition,
    const std::map<std::string, QueryValue> &updates_map) { // Renamed
  if (updates_map.empty()) {
    qInfo("cpporm Session::Updates (by model): No update values provided.");
    return 0LL;
  }
  const ModelMeta &meta = model_condition._getOwnModelMeta();
  QueryBuilder qb = this->Model(meta);

  if (meta.primary_keys_db_names.empty()) {
    return std::unexpected(
        Error(ErrorCode::MappingError,
              "Updates by model instance: No primary key defined for model " +
                  meta.table_name));
  }

  std::map<std::string, QueryValue> pk_conditions;
  for (const auto &pk_db_name : meta.primary_keys_db_names) {
    const FieldMeta *pk_field = meta.findFieldByDbName(pk_db_name);
    if (!pk_field) {
      return std::unexpected(
          Error(ErrorCode::InternalError,
                "Updates by model instance: PK field meta not found for " +
                    pk_db_name));
    }
    std::any pk_val_any = model_condition.getFieldValue(pk_field->cpp_name);
    if (!pk_val_any.has_value()) {
      return std::unexpected(Error(ErrorCode::MappingError,
                                   "Updates by model instance: PK value for " +
                                       pk_db_name +
                                       " is not set in the model."));
    }
    // Use the Session's helper for std::any to QueryValue conversion
    QueryValue qv_pk =
        Session::anyToQueryValueForSessionConvenience(pk_val_any);
    if (std::holds_alternative<std::nullptr_t>(qv_pk) &&
        pk_val_any.has_value()) { // If conversion failed for a non-empty any
      return std::unexpected(
          Error(ErrorCode::MappingError,
                "Updates by model_condition: Unsupported PK type (" +
                    std::string(pk_val_any.type().name()) + ") for field " +
                    pk_db_name));
    }
    pk_conditions[pk_db_name] = qv_pk;
  }

  if (pk_conditions
          .empty()) { // Should be caught by !pk_val_any.has_value() above
    return std::unexpected(Error(
        ErrorCode::MappingError,
        "Updates by model instance: Failed to extract valid PK conditions."));
  }
  qb.Where(pk_conditions);
  // Call the Impl method
  return this->UpdatesImpl(qb, updates_map);
}

} // namespace cpporm