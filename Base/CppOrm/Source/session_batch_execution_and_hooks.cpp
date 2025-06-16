// cpporm/session_batch_execution_and_hooks.cpp
#include "cpporm/model_base.h"
#include "cpporm/session.h"
#include "cpporm/session_priv_batch_helpers.h" // FriendAccess 定义在此

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace cpporm {
namespace internal_batch_helpers {

ExecutionResult
executeBatchSql(Session &session, const QString &sql_to_execute,
                const QVariantList &bindings,
                const std::vector<ModelBase *>
                    &models_in_db_op, // 这些是原始的、准备好进行DB操作的模型
                const OnConflictClause *active_conflict_clause) {
  ExecutionResult result;

  auto exec_pair = FriendAccess::callExecuteQueryInternal(
      session.getDbHandle(), sql_to_execute, bindings);
  result.query_object = std::move(exec_pair.first);
  result.db_error = exec_pair.second;

  if (result.db_error) {
    return result;
  }

  result.rows_affected = result.query_object.numRowsAffected();

  // 根据数据库操作结果和冲突选项，设置 _is_persisted 状态
  // 并将这些模型加入 models_potentially_persisted 列表
  if (result.rows_affected > 0 ||
      (active_conflict_clause &&
       active_conflict_clause->action != OnConflictClause::Action::DoNothing &&
       result.rows_affected >= 0)) {
    // 如果有行受影响，或者有冲突处理（非DO_NOTHING）且行影响数>=0
    for (ModelBase *m : models_in_db_op) {
      if (m) {
        m->_is_persisted =
            true; // <<<<<<<<<<<<<<<<<<<<<<< 关键修复：设置持久化状态
        result.models_potentially_persisted.push_back(m);
      }
    }
  } else if (result.rows_affected == 0 && active_conflict_clause &&
             active_conflict_clause->action ==
                 OnConflictClause::Action::DoNothing) {
    // 对于 DO NOTHING 且0行受影响，模型可能已存在但未被修改。
    // _is_persisted 状态不应改变（如果之前是 false，则保持 false）。
    // 但它们仍然是“已处理”的，所以加入列表供回调。
    for (ModelBase *m : models_in_db_op) {
      if (m) {
        // m->_is_persisted = true; // 错误：这里不应该设置为
        // true，因为没有新插入或更新 如果模型之前不存在，它仍然不是 persisted。
        // 如果它已存在，其 _is_persisted 状态应该由加载它的操作设置。
        // 对于 CreateBatch 的 DO NOTHING 场景，如果记录已存在，
        // _is_persisted 应该保持其原始值（通常是
        // false，因为我们正在尝试创建）。
        result.models_potentially_persisted.push_back(m);
      }
    }
  }
  // 如果 rows_affected < 0 (通常表示错误或非DML语句)
  // models_potentially_persisted 将为空 (除非上面的条件已捕获了错误)。

  return result;
}

void callAfterCreateHooks(
    Session &session,
    const std::vector<ModelBase *> &models_for_hooks, // 改为接收这个列表
    Error &in_out_first_error_encountered) {
  for (ModelBase *model_ptr : models_for_hooks) {
    // 调用钩子前再次确认 _is_persisted，因为ID回填可能会失败，
    // 但DB操作本身可能成功使记录持久化。
    // 或者，钩子应该只为那些不仅DB操作成功，而且ID也成功回填（如果是自增PK）的模型调用。
    // 目前的 successfully_backfilled_models 列表是更准确的。
    // 因此，这里参数应该就是那些真正成功创建并回填ID的模型。
    if (!model_ptr || !model_ptr->_is_persisted) // 确保只为成功持久化的模型调用
      continue;

    Error hook_err = model_ptr->afterCreate(session);
    if (hook_err) {
      if (in_out_first_error_encountered.isOk()) {
        in_out_first_error_encountered = hook_err;
      }
      qWarning() << "callAfterCreateHooks: afterCreate hook failed for a "
                    "model. Error: "
                 << hook_err.toString().c_str();
    }
  }
}

} // namespace internal_batch_helpers
} // namespace cpporm