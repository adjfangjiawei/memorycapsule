// cpporm/session_priv_batch_helpers_fwd.h
#ifndef cpporm_SESSION_PRIV_BATCH_HELPERS_FWD_H
#define cpporm_SESSION_PRIV_BATCH_HELPERS_FWD_H

#include <QString>
#include <QVariant>
#include <string>
#include <utility> // For std::pair
#include <vector>
// 前向声明核心类型，避免循环依赖
namespace cpporm {
class Session;
class ModelBase;
struct ModelMeta;
struct Error; // 假设 Error 定义在 error.h 中，并且被广泛包含
struct OnConflictClause;

namespace internal_batch_helpers {
struct BatchSqlParts;   // 在 session_priv_batch_helpers.h 中定义
struct ExecutionResult; // 在 session_priv_batch_helpers.h 中定义

// 声明将成为 Session 友元的函数
std::pair<std::vector<ModelBase *>, Error> prepareModelsAndSqlPlaceholders(
    Session &session, const std::vector<ModelBase *> &models_in_provider_chunk,
    const ModelMeta &meta,
    const std::vector<std::string> &batch_ordered_db_field_names_cache,
    BatchSqlParts &out_sql_parts);

ExecutionResult executeBatchSql(Session &session, const QString &sql_to_execute,
                                const QVariantList &bindings,
                                const std::vector<ModelBase *> &models_in_db_op,
                                const OnConflictClause *active_conflict_clause);

// 其他在 internal_batch_helpers 中定义但在 session_core.h 中不需要友元的函数，
// 则不需要在此处前向声明。
} // namespace internal_batch_helpers
} // namespace cpporm

#endif // cpporm_SESSION_PRIV_BATCH_HELPERS_FWD_H