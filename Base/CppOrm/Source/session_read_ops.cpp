// cpporm/session_read_ops.cpp
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
// #include "cpporm/qt_db_manager.h" // 通常不需要

#include <QDebug>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <algorithm>

namespace cpporm {

Error Session::FirstImpl(const QueryBuilder &qb, ModelBase &result_model) {
  const ModelMeta *meta = qb.getModelMeta();
  if (!meta) {
    // If QB doesn't have meta (e.g., built via MakeQueryBuilder and Table()),
    // use the meta from the result_model instance.
    meta = &(result_model._getOwnModelMeta());
    if (meta->table_name.empty()) {
      return Error(ErrorCode::InvalidConfiguration,
                   "FirstImpl: Could not determine ModelMeta for query.");
    }
  }

  QueryBuilder local_qb = qb; // Make a mutable copy for Limit(1)
  local_qb.Limit(1);
  auto [sql, params] = local_qb.buildSelectSQL();

  if (sql.isEmpty()) {
    return Error(ErrorCode::StatementPreparationError,
                 "Failed to build SQL for First operation.");
  }

  auto [query, exec_err] =
      execute_query_internal(this->db_handle_, sql, params);
  if (exec_err) {
    return exec_err;
  }

  if (query.next()) {
    Error map_err = mapRowToModel(query, result_model, *meta);
    if (map_err) {
      qWarning() << "cpporm Session::FirstImpl: Error mapping row:"
                 << map_err.toString().c_str();
      return map_err;
    }
    result_model._is_persisted = true;
    Error hook_err = result_model.afterFind(*this);
    if (hook_err)
      return hook_err;

    // Preloading logic: If there are preload requests, handle them.
    // This logic is now correctly placed within the IQueryExecutor (Session)
    // implementation.
    if (!qb.getPreloadRequests().empty()) {
      // Convert ModelBase& to a vector of raw pointers for
      // processPreloadsInternal
      std::vector<ModelBase *> models_for_preload = {&result_model};
      Error preload_err = this->processPreloadsInternal(qb, models_for_preload);
      if (preload_err) {
        qWarning()
            << "Session::FirstImpl: Preloading failed after fetching model: "
            << preload_err.toString().c_str();
        // Decide if preload failure should make the whole operation fail
        // return preload_err;
      }
    }
    return make_ok();
  } else {
    return Error(ErrorCode::RecordNotFound,
                 "No record found for First operation.");
  }
}

Error Session::FindImpl(
    const QueryBuilder &qb,
    std::vector<std::unique_ptr<ModelBase>> &results_vector,
    std::function<std::unique_ptr<ModelBase>()> element_type_factory) {
  if (!element_type_factory) {
    return Error(ErrorCode::InternalError,
                 "Element type factory function is null for Find operation.");
  }

  const ModelMeta *meta_for_query = qb.getModelMeta();
  if (!meta_for_query) {
    auto temp_instance = element_type_factory();
    if (temp_instance &&
        !temp_instance->_getOwnModelMeta().table_name.empty()) {
      meta_for_query = &(temp_instance->_getOwnModelMeta());
    } else {
      return Error(ErrorCode::InvalidConfiguration,
                   "FindImpl: Could not determine ModelMeta for query from "
                   "QueryBuilder or factory.");
    }
  }

  auto [sql, params] = qb.buildSelectSQL();
  if (sql.isEmpty()) {
    return Error(ErrorCode::StatementPreparationError,
                 "Failed to build SQL for Find operation.");
  }

  auto [query, exec_err] =
      execute_query_internal(this->db_handle_, sql, params);
  if (exec_err) {
    return exec_err;
  }

  results_vector.clear();
  while (query.next()) {
    std::unique_ptr<ModelBase> new_element = element_type_factory();
    if (!new_element) {
      return Error(ErrorCode::InternalError,
                   "Element factory returned nullptr inside Find loop.");
    }
    Error map_err = mapRowToModel(query, *new_element, *meta_for_query);
    if (map_err) {
      qWarning() << "cpporm Session::FindImpl: Error mapping row: "
                 << map_err.toString().c_str() << ". SQL was: " << sql;
      return map_err;
    }
    new_element->_is_persisted = true;
    Error hook_err = new_element->afterFind(*this);
    if (hook_err) {
      qWarning()
          << "cpporm Session::FindImpl: afterFind hook failed for an element: "
          << hook_err.toString().c_str();
    }
    results_vector.push_back(std::move(new_element));
  }

  // Preloading logic for FindImpl
  if (!results_vector.empty() && !qb.getPreloadRequests().empty()) {
    // processPreloads now correctly takes a vector of unique_ptr
    Error preload_err = this->processPreloads(qb, results_vector);
    if (preload_err) {
      return preload_err;
    }
  }
  return make_ok();
}

std::expected<int64_t, Error> Session::CountImpl(const QueryBuilder &qb_const) {
  QueryBuilder qb = qb_const; // 创建可修改副本
  if (!qb.getGroupClause().empty()) {
    qWarning("cpporm Session::CountImpl: Count() called with existing GROUP "
             "BY clause. Clearing GROUP BY for total count.");
    qb.Group("");
  }
  qb.Select("COUNT(*)");
  qb.Order("");
  qb.Limit(-1);
  qb.Offset(-1);
  if (!qb.getState_().preload_requests_.empty()) {
    // It's good practice to clear preloads for a COUNT query as they are
    // irrelevant.
    QueryBuilderState &mutable_state =
        const_cast<QueryBuilder &>(qb).getState_();
    mutable_state.preload_requests_.clear();
  }

  auto [sql, params] = qb.buildSelectSQL();
  if (sql.isEmpty()) {
    return std::unexpected(Error(ErrorCode::StatementPreparationError,
                                 "Failed to build SQL for Count operation."));
  }
  auto [query, err] = execute_query_internal(this->db_handle_, sql, params);
  if (err) {
    return std::unexpected(err);
  }
  if (query.next()) {
    bool ok_conversion;
    int64_t count_val = query.value(0).toLongLong(&ok_conversion);
    if (ok_conversion) {
      return count_val;
    } else {
      return std::unexpected(
          Error(ErrorCode::MappingError,
                "Failed to convert COUNT(*) result to integer. Value: " +
                    query.value(0).toString().toStdString()));
    }
  } else {
    qWarning() << "cpporm Session::CountImpl: COUNT(*) query returned no rows "
                  "(unexpected). SQL:"
               << sql;
    return std::unexpected(Error(ErrorCode::QueryExecutionError,
                                 "COUNT(*) query returned no rows."));
  }
}

} // namespace cpporm