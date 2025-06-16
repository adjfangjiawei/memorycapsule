// cpporm/session_batch_id_backfillers.cpp
#include "cpporm/builder_parts/query_builder_state.h"
#include "cpporm/model_base.h"
#include "cpporm/session.h"
#include "cpporm/session_priv_batch_helpers.h"

#include <QDebug>
#include <QSqlDriver>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm> // For std::find

namespace cpporm {
namespace internal_batch_helpers {

std::vector<ModelBase *> backfillIdsFromReturning(
    QSqlQuery &executed_query,
    const ModelMeta &meta, // meta 未在此函数中直接使用，但保留
    const std::vector<ModelBase *>
        &models_to_backfill_from, // 这些模型应已由 executeBatchSql 标记
                                  // _is_persisted = true
    const std::string &pk_cpp_name_str, const std::type_index &pk_cpp_type) {
  std::vector<ModelBase *> successfully_backfilled_models;
  if (models_to_backfill_from.empty()) {
    return successfully_backfilled_models;
  }

  // 迭代 models_to_backfill_from，这些是 executeBatchSql 认为受影响的模型
  for (ModelBase *model_to_backfill : models_to_backfill_from) {
    if (!model_to_backfill)
      continue; // 安全检查
    // 此处不再检查 model_to_backfill->_is_persisted，因为 executeBatchSql
    // 已处理。 我们期望 RETURNING 子句为每个受影响的行返回一个 ID。

    if (!executed_query.next()) {
      qWarning() << "backfillIdsFromReturning: RETURNING clause provided fewer "
                    "ID rows than the number of potentially persisted models "
                    "in the batch for table "
                 << QString::fromStdString(meta.table_name);
      break;
    }
    QVariant id_val_ret = executed_query.value(0);
    std::any pk_any_val;
    bool conv_ok = false;
    Session::qvariantToAny(id_val_ret, pk_cpp_type, pk_any_val, conv_ok);

    if (conv_ok) {
      Error set_err =
          model_to_backfill->setFieldValue(pk_cpp_name_str, pk_any_val);
      if (set_err) {
        qWarning()
            << "backfillIdsFromReturning: Error setting PK value for table "
            << QString::fromStdString(meta.table_name) << ", field "
            << QString::fromStdString(pk_cpp_name_str) << " after RETURNING:"
            << QString::fromStdString(set_err.toString());
        // 即使设置失败，模型在数据库层面是持久化的，但应用层面ID可能不同步
        // 此时不应该将其从“已持久化”中移除，但它不是“成功回填”的
      } else {
        successfully_backfilled_models.push_back(model_to_backfill);
      }
    } else {
      qWarning() << "backfillIdsFromReturning: PK backfill conversion failed "
                    "for RETURNING. DB val:"
                 << id_val_ret.toString() << "to C++ type" << pk_cpp_type.name()
                 << " for table " << QString::fromStdString(meta.table_name);
    }
  }
  return successfully_backfilled_models;
}

std::vector<ModelBase *> backfillIdsFromLastInsertId(
    QSqlQuery &executed_query, const Session &session, const ModelMeta &meta,
    const std::vector<ModelBase *>
        &models_to_backfill_from, // 这些模型应已由 executeBatchSql 标记
                                  // _is_persisted = true
    long long total_rows_affected_by_query, const std::string &pk_cpp_name_str,
    const std::type_index &pk_cpp_type,
    const OnConflictClause *active_conflict_clause) {
  std::vector<ModelBase *> successfully_backfilled_models;
  if (models_to_backfill_from.empty() || total_rows_affected_by_query <= 0) {
    return successfully_backfilled_models;
  }

  QVariant first_id_qval = executed_query.lastInsertId();
  if (!first_id_qval.isValid() ||
      (first_id_qval.toLongLong() == 0 &&
       first_id_qval.toInt() ==
           0)) { // 检查是否为0，因为某些驱动可能返回0表示无ID
    // lastInsertId 不可用或为0，无法回填
    // 只有当确定0不是一个有效的自增ID时，才跳过。
    // 对于某些序列，0可能是有效的。但通常不是。
    bool is_zero_actually = false;
    bool ok_ll = false;
    long long ll_val = first_id_qval.toLongLong(&ok_ll);
    if (ok_ll && ll_val == 0)
      is_zero_actually = true;
    bool ok_int = false;
    int int_val = first_id_qval.toInt(&ok_int);
    if (ok_int && int_val == 0)
      is_zero_actually = true;

    if (!first_id_qval.isValid() ||
        (first_id_qval.isNull() && !is_zero_actually) ||
        (is_zero_actually && pk_cpp_type != typeid(int) &&
         pk_cpp_type != typeid(long long))) {
      qWarning() << "backfillIdsFromLastInsertId: lastInsertId is invalid, "
                    "null, or zero (and PK is not int/longlong) for table "
                 << QString::fromStdString(meta.table_name)
                 << ". Value: " << first_id_qval.toString();
      return successfully_backfilled_models;
    }
  }

  QString db_driver_name_upper = session.getDbHandle().driverName().toUpper();

  // 仅为 models_to_backfill_from 中的模型尝试回填
  if (models_to_backfill_from.size() == 1 &&
      total_rows_affected_by_query >= 1) { // 确保至少有一行受影响
    ModelBase *single_model = models_to_backfill_from[0];
    if (single_model) { // 不需要再次检查 _is_persisted，因为它应已被
                        // executeBatchSql 设置
      std::any pk_any_val;
      bool conv_ok = false;
      Session::qvariantToAny(first_id_qval, pk_cpp_type, pk_any_val, conv_ok);
      if (conv_ok) {
        Error set_err =
            single_model->setFieldValue(pk_cpp_name_str, pk_any_val);
        if (set_err) {
          qWarning() << "backfillIdsFromLastInsertId: Error setting PK value "
                        "(single) for table "
                     << QString::fromStdString(meta.table_name) << ":"
                     << QString::fromStdString(set_err.toString());
        } else {
          successfully_backfilled_models.push_back(single_model);
        }
      } else {
        qWarning() << "backfillIdsFromLastInsertId: PK backfill (single, "
                      "lastInsertId) conversion failed for table "
                   << QString::fromStdString(meta.table_name)
                   << ". DB val: " << first_id_qval.toString()
                   << " to C++ type " << pk_cpp_type.name();
      }
    }
  } else if (
      (db_driver_name_upper.contains("MYSQL") ||
       db_driver_name_upper.contains("MARIADB")) && // MySQL/MariaDB
      total_rows_affected_by_query > 0 &&
      total_rows_affected_by_query <=
          static_cast<long long>(
              models_to_backfill_from
                  .size()) &&     // 受影响行数不应超过尝试插入的行数
      (!active_conflict_clause || // 纯插入
       (active_conflict_clause && // 或冲突导致了插入/更新，且MySQL通常对连续插入返回第一个ID
        (active_conflict_clause->action ==
             OnConflictClause::Action::UpdateAllExcluded ||
         active_conflict_clause->action ==
             OnConflictClause::Action::UpdateSpecific)))) {
    long long first_id_ll = first_id_qval.toLongLong();
    // 仅当受影响的行数与我们期望回填的模型数完全匹配时，才假设连续ID是可靠的
    // 并且这通常只在没有复杂ON DUPLICATE KEY UPDATE（可能跳过一些行）时适用
    if (total_rows_affected_by_query ==
        static_cast<long long>(models_to_backfill_from.size())) {
      for (size_t k = 0; k < models_to_backfill_from.size(); ++k) {
        ModelBase *current_model = models_to_backfill_from[k];
        if (!current_model)
          continue;

        long long current_model_id_ll = first_id_ll + static_cast<long long>(k);
        std::any pk_any_val_seq;
        bool conv_ok_seq = false;
        QVariant qv_current_id(current_model_id_ll);
        Session::qvariantToAny(qv_current_id, pk_cpp_type, pk_any_val_seq,
                               conv_ok_seq);
        if (conv_ok_seq) {
          Error set_err =
              current_model->setFieldValue(pk_cpp_name_str, pk_any_val_seq);
          if (set_err) {
            qWarning() << "backfillIdsFromLastInsertId: Error setting PK value "
                          "(sequential) for table "
                       << QString::fromStdString(meta.table_name) << ":"
                       << QString::fromStdString(set_err.toString());
          } else {
            successfully_backfilled_models.push_back(current_model);
          }
        } else {
          qWarning() << "backfillIdsFromLastInsertId: PK backfill (sequential, "
                        "lastInsertId) "
                        "conversion failed for derived ID:"
                     << current_model_id_ll << " for table "
                     << QString::fromStdString(meta.table_name);
        }
      }
    } else if (!models_to_backfill_from.empty() &&
               total_rows_affected_by_query > 0) {
      // 如果受影响的行数不完全匹配，但至少有一个，则只尝试回填第一个模型（如果它是models_to_backfill_from的一部分）
      // 这种情况可能发生在ON DUPLICATE KEY UPDATE部分更新部分插入时
      ModelBase *first_persisted_model = nullptr;
      for (ModelBase *m :
           models_to_backfill_from) { // 找到 models_to_backfill_from
                                      // 中第一个被标记为持久化的
        if (m && m->_is_persisted) {
          first_persisted_model = m;
          break;
        }
      }
      if (first_persisted_model) {
        std::any pk_any_val;
        bool conv_ok = false;
        Session::qvariantToAny(first_id_qval, pk_cpp_type, pk_any_val, conv_ok);
        if (conv_ok) {
          Error set_err =
              first_persisted_model->setFieldValue(pk_cpp_name_str, pk_any_val);
          if (set_err) { /* qWarning */
          } else {
            successfully_backfilled_models.push_back(first_persisted_model);
          }
        } else { /* qWarning */
        }
      }
      qWarning() << "backfillIdsFromLastInsertId: lastInsertId may not be "
                    "reliable for all rows in this batch "
                 << "operation for table "
                 << QString::fromStdString(meta.table_name)
                 << ". Rows affected (" << total_rows_affected_by_query
                 << ") != models in batch (" << models_to_backfill_from.size()
                 << "). Only first ID (if applicable) might be accurate.";
    }

  } else if (db_driver_name_upper == "QSQLITE" &&
             total_rows_affected_by_query == 1 &&
             models_to_backfill_from.size() >= 1) {
    // SQLite 的 lastInsertId() 返回最后插入行的
    // ROWID。如果批量插入多行，它只返回最后一行的。
    // 所以，只有当批量大小为1（或者我们只关心最后一个）时才可靠。
    // 或者，如果 total_rows_affected_by_query 也是1，这意味着确实只插入了一行。
    ModelBase *model_to_set = models_to_backfill_from.back(); // 假设是最后一个
    if (models_to_backfill_from.size() == 1)
      model_to_set = models_to_backfill_from[0];

    if (model_to_set) {
      std::any pk_any_val;
      bool conv_ok = false;
      Session::qvariantToAny(first_id_qval, pk_cpp_type, pk_any_val, conv_ok);
      if (conv_ok) {
        Error set_err =
            model_to_set->setFieldValue(pk_cpp_name_str, pk_any_val);
        if (set_err) { /* qWarning */
        } else {
          successfully_backfilled_models.push_back(model_to_set);
        }
      } else { /* qWarning */
      }
    }
  } else {
    qWarning() << "backfillIdsFromLastInsertId: lastInsertId is not reliably "
                  "applicable for this batch operation "
               << "on driver " << db_driver_name_upper << " for table "
               << QString::fromStdString(meta.table_name)
               << ". Models in batch: " << models_to_backfill_from.size()
               << ", Rows affected: " << total_rows_affected_by_query;
  }
  return successfully_backfilled_models;
}

} // namespace internal_batch_helpers
} // namespace cpporm