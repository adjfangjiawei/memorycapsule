// cpporm/session_create_batch_ops.cpp
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_priv_batch_helpers.h" // 包含新的私有助手声明

#include <QDateTime>
#include <QDebug>
#include <QMetaType>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm>
#include <utility>
#include <vector>

namespace cpporm {

// 实现 CreateBatchProviderInternal
Error Session::CreateBatchProviderInternal(
    QueryBuilder qb_prototype,
    std::function<std::optional<std::vector<ModelBase *>>()>
        data_batch_provider_base,
    std::function<
        void(const std::vector<ModelBase *> &processed_batch_models_with_ids,
             Error batch_error)>
        per_db_batch_completion_callback,
    const OnConflictClause *conflict_options_override) {

  const ModelMeta *meta_ptr = qb_prototype.getModelMeta();
  if (!meta_ptr) {
    return Error(ErrorCode::InvalidConfiguration,
                 "CreateBatchProviderInternal: QB prototype has no ModelMeta.");
  }
  const ModelMeta &meta = *meta_ptr;

  const OnConflictClause *active_conflict_clause = conflict_options_override;
  if (!active_conflict_clause && qb_prototype.getOnConflictClause()) {
    active_conflict_clause = qb_prototype.getOnConflictClause();
  }
  if (!active_conflict_clause && temp_on_conflict_clause_) {
    active_conflict_clause = temp_on_conflict_clause_.get();
  }
  bool clear_session_temp_on_conflict_at_end =
      (active_conflict_clause == temp_on_conflict_clause_.get() &&
       !conflict_options_override && !qb_prototype.getOnConflictClause());

  if (!data_batch_provider_base || !per_db_batch_completion_callback) {
    if (clear_session_temp_on_conflict_at_end)
      this->clearTempOnConflictClause();
    return Error(ErrorCode::InvalidConfiguration,
                 "CreateBatchProviderInternal: Null data provider or "
                 "completion callback.");
  }

  const FieldMeta *auto_inc_pk_field = meta.getPrimaryField();
  bool has_auto_inc_pk =
      (auto_inc_pk_field &&
       has_flag(auto_inc_pk_field->flags, FieldFlag::AutoIncrement));
  std::string pk_db_name_str = has_auto_inc_pk
                                   ? auto_inc_pk_field->db_name
                                   : ""; // DB name for RETURNING or metadata
  std::string pk_cpp_name_str =
      has_auto_inc_pk ? auto_inc_pk_field->cpp_name : "";
  std::type_index pk_cpp_type =
      has_auto_inc_pk ? auto_inc_pk_field->cpp_type : typeid(void);
  bool driver_supports_last_insert_id =
      db_handle_.driver()->hasFeature(QSqlDriver::LastInsertId);
  QString db_driver_name_upper = db_handle_.driverName().toUpper(); // Get once
  bool use_returning_for_batch =
      (db_driver_name_upper == "QPSQL" && has_auto_inc_pk &&
       !pk_db_name_str.empty() &&
       (!active_conflict_clause ||
        (active_conflict_clause && active_conflict_clause->action !=
                                       OnConflictClause::Action::DoNothing)));

  std::vector<std::string> batch_ordered_db_field_names_cache;
  std::optional<std::vector<ModelBase *>> current_provider_chunk_opt;
  Error first_error_encountered_in_loop = make_ok();

  while (
      (current_provider_chunk_opt = data_batch_provider_base()).has_value()) {
    std::vector<ModelBase *> &models_in_current_provider_chunk =
        current_provider_chunk_opt.value();

    if (models_in_current_provider_chunk.empty()) {
      per_db_batch_completion_callback({}, make_ok());
      continue;
    }

    // 1. 确定字段顺序 (如果尚未确定)
    if (batch_ordered_db_field_names_cache.empty()) {
      ModelBase *first_valid_model = nullptr;
      for (ModelBase *m : models_in_current_provider_chunk) {
        if (m) {
          first_valid_model = m;
          break;
        }
      }
      if (first_valid_model) {
        // 使用 Session 的私有 extractModelData (通过外部助手或友元访问)
        internal::SessionModelDataForWrite first_data =
            this->extractModelData(*first_valid_model, meta, false, true);
        if (first_data.fields_to_write.empty() &&
            !first_data.has_auto_increment_pk) {
          Error err =
              Error(ErrorCode::MappingError,
                    "First model in batch has no writable fields and no "
                    "auto-inc PK. Batch cannot determine field order.");
          per_db_batch_completion_callback(models_in_current_provider_chunk,
                                           err);
          if (first_error_encountered_in_loop.isOk())
            first_error_encountered_in_loop = err;
          continue;
        }
        for (const auto &pair : first_data.fields_to_write) {
          batch_ordered_db_field_names_cache.push_back(
              pair.first.toStdString());
        }
      } else {
        per_db_batch_completion_callback(models_in_current_provider_chunk,
                                         make_ok()); // Chunk of nullptrs
        continue;
      }
    }

    // 特殊处理 PG 纯自增批量插入 (分解为单个)
    if (batch_ordered_db_field_names_cache.empty() && has_auto_inc_pk &&
        db_driver_name_upper == "QPSQL" &&
        models_in_current_provider_chunk.size() > 1) {
      std::vector<ModelBase *> successfully_processed_this_chunk_pg_special;
      bool error_in_this_pg_chunk = false;
      for (ModelBase *model_to_insert_individually :
           models_in_current_provider_chunk) {
        if (!model_to_insert_individually)
          continue;
        QueryBuilder single_model_qb(this, connection_name_, &meta);
        auto single_res =
            this->CreateImpl(single_model_qb, *model_to_insert_individually,
                             active_conflict_clause);

        if (single_res.has_value() &&
            model_to_insert_individually->_is_persisted) {
          successfully_processed_this_chunk_pg_special.push_back(
              model_to_insert_individually);
        } else {
          Error err_single =
              single_res.has_value() ? make_ok() : single_res.error();
          if (!single_res.has_value() &&
              model_to_insert_individually
                  ->_is_persisted) { // persisted but error (unlikely)
            err_single =
                Error(ErrorCode::InternalError,
                      "Model persisted but CreateImpl returned error.");
          } else if (single_res.has_value() &&
                     !model_to_insert_individually
                          ->_is_persisted) { // no error but not persisted
            err_single = Error(ErrorCode::InternalError,
                               "CreateImpl success but model not persisted.");
          }
          per_db_batch_completion_callback({model_to_insert_individually},
                                           err_single);
          if (first_error_encountered_in_loop.isOk() && err_single)
            first_error_encountered_in_loop = err_single;
          error_in_this_pg_chunk = true;
        }
      }
      if (!successfully_processed_this_chunk_pg_special.empty() &&
          !error_in_this_pg_chunk) {
        per_db_batch_completion_callback(
            successfully_processed_this_chunk_pg_special, make_ok());
      } // Individual errors already reported by callback
      continue;
    }

    internal_batch_helpers::BatchSqlParts sql_parts_for_this_db_batch;
    auto [models_prepared_for_db_op, prepare_error] =
        internal_batch_helpers::prepareModelsAndSqlPlaceholders(
            *this, models_in_current_provider_chunk, meta,
            batch_ordered_db_field_names_cache, sql_parts_for_this_db_batch);

    if (prepare_error) {
      if (first_error_encountered_in_loop.isOk())
        first_error_encountered_in_loop = prepare_error;
      if (models_prepared_for_db_op
              .empty()) { // 如果准备后完全为空，则整个 chunk 都有问题
        per_db_batch_completion_callback(models_in_current_provider_chunk,
                                         prepare_error);
      } else { // 部分准备成功，部分失败（失败的已在
               // prepareModelsAndSqlPlaceholders 中单独回调）
        // 对于成功准备的部分，我们是否继续？或者因为批次中部分失败而整体失败？
        // 目前 prepareModelsAndSqlPlaceholders 不会单独回调，它返回第一个错误。
        // 所以如果 prepare_error 非空，意味着整个 chunk 的准备阶段有问题。
        per_db_batch_completion_callback(models_in_current_provider_chunk,
                                         prepare_error);
      }
      continue;
    }
    if (models_prepared_for_db_op.empty()) {
      per_db_batch_completion_callback(models_in_current_provider_chunk,
                                       make_ok()); // 无可操作模型
      continue;
    }

    Error build_sql_err = internal_batch_helpers::buildFullBatchSqlStatement(
        *this, qb_prototype, meta, batch_ordered_db_field_names_cache,
        active_conflict_clause, sql_parts_for_this_db_batch);

    if (build_sql_err || !sql_parts_for_this_db_batch.can_proceed) {
      Error final_build_err = build_sql_err
                                  ? build_sql_err
                                  : Error(ErrorCode::StatementPreparationError,
                                          "Failed to build final SQL for batch "
                                          "(can_proceed is false).");
      per_db_batch_completion_callback(models_prepared_for_db_op,
                                       final_build_err);
      if (first_error_encountered_in_loop.isOk())
        first_error_encountered_in_loop = final_build_err;
      continue;
    }

    internal_batch_helpers::ExecutionResult exec_result =
        internal_batch_helpers::executeBatchSql(
            *this, sql_parts_for_this_db_batch.final_sql_statement,
            sql_parts_for_this_db_batch.final_bindings,
            models_prepared_for_db_op, // 这些是实际参与DB操作的模型
            active_conflict_clause);

    if (exec_result.db_error) {
      per_db_batch_completion_callback(models_prepared_for_db_op,
                                       exec_result.db_error);
      if (first_error_encountered_in_loop.isOk())
        first_error_encountered_in_loop = exec_result.db_error;
      continue;
    }

    std::vector<ModelBase *> successfully_backfilled_models;
    if (has_auto_inc_pk && !exec_result.models_potentially_persisted.empty()) {
      if (use_returning_for_batch) {
        successfully_backfilled_models =
            internal_batch_helpers::backfillIdsFromReturning(
                exec_result.query_object, meta,
                exec_result.models_potentially_persisted, pk_cpp_name_str,
                pk_cpp_type);
      } else if (driver_supports_last_insert_id) {
        successfully_backfilled_models =
            internal_batch_helpers::backfillIdsFromLastInsertId(
                exec_result.query_object, *this, meta,
                exec_result.models_potentially_persisted,
                exec_result.rows_affected, pk_cpp_name_str, pk_cpp_type,
                active_conflict_clause);
      } else { // 无 RETURNING 且无 LastInsertId，但操作成功
        // 此时 successfully_backfilled_models 将为空，但
        // models_potentially_persisted 中的模型 可能已被标记为
        // _is_persisted。回调时传递 models_potentially_persisted。
        for (ModelBase *m : exec_result.models_potentially_persisted) {
          if (m && m->_is_persisted)
            successfully_backfilled_models.push_back(m);
        }
      }
    } else if (!exec_result.models_potentially_persisted.empty() &&
               exec_result.rows_affected >= 0) { // 无自增PK，但操作成功
      for (ModelBase *m : exec_result.models_potentially_persisted) {
        if (m) { // 之前在 executeBatchSql 中已根据行影响数和冲突选项初步标记
          if (m->_is_persisted)
            successfully_backfilled_models.push_back(m);
        }
      }
    }

    internal_batch_helpers::callAfterCreateHooks(
        *this, successfully_backfilled_models, first_error_encountered_in_loop);

    // 决定回调中传递哪个列表：
    // 如果ID回填成功了一些模型，则传递 successfully_backfilled_models。
    // 如果ID回填没有适用或失败，但DB操作本身成功，则传递
    // exec_result.models_potentially_persisted （这些模型已被标记为
    // _is_persisted，只是没有获得ID）。
    // 回调的目的是通知哪些模型被“成功处理”（插入/更新，并尽可能回填ID）。
    if (!successfully_backfilled_models.empty()) {
      per_db_batch_completion_callback(successfully_backfilled_models,
                                       make_ok());
    } else if (exec_result.rows_affected >= 0 &&
               exec_result.db_error.isOk()) { // DB操作成功，但可能无ID回填
      // 检查 models_prepared_for_db_op 中哪些被标记为 _is_persisted
      std::vector<ModelBase *> final_persisted_for_callback;
      for (ModelBase *m : models_prepared_for_db_op) {
        if (m && m->_is_persisted)
          final_persisted_for_callback.push_back(m);
      }
      per_db_batch_completion_callback(final_persisted_for_callback, make_ok());
    } else { // 如果没有成功回填的模型，并且DB操作可能有问题（虽然上面已检查
             // exec_result.db_error）
      per_db_batch_completion_callback(
          {}, exec_result.db_error.isOk()
                  ? Error(ErrorCode::UnknownError,
                          "Batch operation reported success but no models "
                          "processed or IDs backfilled.")
                  : exec_result.db_error);
    }

  } // 结束 provider while 循环

  if (clear_session_temp_on_conflict_at_end) {
    this->clearTempOnConflictClause();
  }

  return first_error_encountered_in_loop;
}

// 实现 CreateBatchWithMeta
std::expected<size_t, Error> Session::CreateBatchWithMeta(
    const ModelMeta &meta, const std::vector<ModelBase *> &models_to_create,
    size_t internal_db_batch_size_hint,
    const OnConflictClause *conflict_options_override) {

  if (models_to_create.empty())
    return 0;

  QueryBuilder qb_proto = this->Model(meta);

  size_t provider_current_idx = 0;
  auto internal_vector_provider = [&models_to_create, provider_current_idx,
                                   internal_db_batch_size_hint]() mutable
      -> std::optional<std::vector<ModelBase *>> {
    if (provider_current_idx >= models_to_create.size()) {
      return std::nullopt;
    }
    std::vector<ModelBase *> chunk;
    size_t end_idx =
        std::min(models_to_create.size(),
                 provider_current_idx + internal_db_batch_size_hint);
    for (size_t i = provider_current_idx; i < end_idx; ++i) {
      if (models_to_create[i]) {
        chunk.push_back(models_to_create[i]);
      }
    }
    provider_current_idx = end_idx;

    if (chunk.empty() && provider_current_idx < models_to_create.size()) {
      return std::vector<ModelBase *>();
    }
    if (chunk.empty())
      return std::nullopt;
    return chunk;
  };

  size_t total_successfully_persisted_accumulator = 0;
  Error first_error_encountered_overall = make_ok();

  auto per_db_batch_completion_callback_for_vector =
      [&total_successfully_persisted_accumulator,
       &first_error_encountered_overall](
          const std::vector<ModelBase *> &processed_batch_models_with_ids,
          Error op_error) {
        if (op_error) {
          if (first_error_encountered_overall.isOk()) {
            first_error_encountered_overall = op_error;
          }
        } else {
          for (ModelBase *bm : processed_batch_models_with_ids) {
            if (bm && bm->_is_persisted) {
              total_successfully_persisted_accumulator++;
            }
          }
        }
      };

  Error provider_loop_err = this->CreateBatchProviderInternal(
      qb_proto, internal_vector_provider,
      per_db_batch_completion_callback_for_vector, conflict_options_override);

  if (provider_loop_err) {
    return std::unexpected(provider_loop_err);
  }
  if (first_error_encountered_overall) {
    return std::unexpected(first_error_encountered_overall);
  }

  return total_successfully_persisted_accumulator;
}

} // namespace cpporm