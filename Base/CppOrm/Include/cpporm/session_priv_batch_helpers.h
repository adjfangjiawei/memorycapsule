// cpporm/session_priv_batch_helpers.h
#ifndef cpporm_SESSION_PRIV_BATCH_HELPERS_H
#define cpporm_SESSION_PRIV_BATCH_HELPERS_H

#include "cpporm/error.h"
#include "cpporm/session_fwd.h"
#include "cpporm/session_types.h" // <<<<<<<< 包含 internal::SessionModelDataForWrite 定义
// #include "cpporm/builder_parts/query_builder_state_fwd.h" //
// OnConflictClause 应该通过 session_types.h -> query_builder_state.h 引入

#include <QSqlDatabase> // FriendAccess::callExecuteQueryInternal 需要
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QVariantList> // 包含 Qt 类型
#include <string>
#include <typeindex>
#include <vector>

namespace cpporm {

// 前向声明 Session 内部的 extractModelData (如果需要被 FriendAccess
// 以外的函数访问，但通常不需要) class Session; // 已在 session_fwd.h 中

class ModelBase;
struct ModelMeta;
struct FieldMeta;
struct OnConflictClause; // 应该通过 session_types.h -> query_builder_state.h
                         // 引入
class QueryBuilder;

namespace internal_batch_helpers {

// FriendAccess 类的定义，用于安全地访问 Session 的私有成员
class FriendAccess {
public:
  static internal::SessionModelDataForWrite
  callExtractModelData(Session &s, const ModelBase &model_instance,
                       const ModelMeta &meta, bool for_update,
                       bool include_timestamps_even_if_null);

  static std::pair<QSqlQuery, Error> callExecuteQueryInternal(
      // Session& s, // 不需要 Session 实例，因为 execute_query_internal
      // 是静态的
      QSqlDatabase db, // 直接传递 QSqlDatabase
      const QString &sql, const QVariantList &ms);

  static void callAutoSetTimestamps(Session &s, ModelBase &model_instance,
                                    const ModelMeta &meta, bool is_create_op);
};

struct BatchSqlParts {
  QString sql_insert_base;
  QStringList row_placeholders;
  QVariantList all_values_flattened;
  QString sql_on_conflict_suffix;
  QVariantList conflict_suffix_bindings;
  QString final_sql_statement;
  QVariantList final_bindings;
  bool can_proceed = false;
};

struct ExecutionResult {
  QSqlQuery query_object;
  long long rows_affected = -1;
  Error db_error = make_ok();
  std::vector<ModelBase *> models_potentially_persisted;
};

// --- 函数声明 ---

std::pair<std::vector<ModelBase *>, Error> prepareModelsAndSqlPlaceholders(
    Session &session, const std::vector<ModelBase *> &models_in_provider_chunk,
    const ModelMeta &meta,
    const std::vector<std::string> &batch_ordered_db_field_names_cache, // IN
    BatchSqlParts &out_sql_parts                                        // OUT
);

Error buildFullBatchSqlStatement(
    const Session &session, const QueryBuilder &qb_prototype,
    const ModelMeta &meta,
    const std::vector<std::string> &batch_ordered_db_field_names_cache,
    const OnConflictClause *active_conflict_clause,
    BatchSqlParts &in_out_sql_parts // IN/OUT
);

ExecutionResult executeBatchSql(
    Session &session, // Pass Session to get db_handle for FriendAccess
    const QString &sql_to_execute, const QVariantList &bindings,
    const std::vector<ModelBase *> &models_in_db_op,
    const OnConflictClause *active_conflict_clause);

std::vector<ModelBase *> backfillIdsFromReturning(
    QSqlQuery &executed_query, const ModelMeta &meta,
    const std::vector<ModelBase *> &models_to_backfill_from,
    const std::string &pk_cpp_name_str, const std::type_index &pk_cpp_type);

std::vector<ModelBase *> backfillIdsFromLastInsertId(
    QSqlQuery &executed_query, const Session &session, const ModelMeta &meta,
    const std::vector<ModelBase *> &models_to_backfill_from,
    long long total_rows_affected_by_query, const std::string &pk_cpp_name_str,
    const std::type_index &pk_cpp_type,
    const OnConflictClause *active_conflict_clause);

void callAfterCreateHooks(Session &session,
                          const std::vector<ModelBase *>
                              &successfully_persisted_and_backfilled_models,
                          Error &in_out_first_error_encountered);

} // namespace internal_batch_helpers
} // namespace cpporm

#endif // cpporm_SESSION_PRIV_BATCH_HELPERS_H