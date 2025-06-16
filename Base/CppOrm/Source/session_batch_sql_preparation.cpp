// cpporm/session_batch_sql_preparation.cpp
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_priv_batch_helpers.h" // 包含声明和 FriendAccess 定义

#include <QDebug>
#include <QStringList>
#include <QVariant>

namespace cpporm {
namespace internal_batch_helpers {

std::pair<std::vector<ModelBase *>, Error> prepareModelsAndSqlPlaceholders(
    Session &session, const std::vector<ModelBase *> &models_in_provider_chunk,
    const ModelMeta &meta,
    const std::vector<std::string> &batch_ordered_db_field_names_cache,
    BatchSqlParts &out_sql_parts) {
  std::vector<ModelBase *> models_prepared_for_sql_build;
  models_prepared_for_sql_build.reserve(models_in_provider_chunk.size());
  Error first_prepare_error = make_ok();

  out_sql_parts.all_values_flattened.clear();
  out_sql_parts.row_placeholders.clear();

  QString db_driver_name_upper = session.getDbHandle().driverName().toUpper();

  for (ModelBase *model_ptr : models_in_provider_chunk) {
    if (!model_ptr)
      continue;

    Error hook_err = model_ptr->beforeCreate(session);
    if (hook_err) {
      if (first_prepare_error.isOk())
        first_prepare_error = hook_err;
      continue;
    }
    FriendAccess::callAutoSetTimestamps(session, *model_ptr, meta, true);

    // 使用 FriendAccess 调用 Session 的私有 extractModelData
    internal::SessionModelDataForWrite model_data_struct =
        FriendAccess::callExtractModelData(session, *model_ptr, meta, false,
                                           true);

    QStringList current_model_placeholders_segment;
    bool model_can_be_inserted_this_pass = false;
    bool is_pure_auto_inc_pk =
        batch_ordered_db_field_names_cache.empty() && meta.getPrimaryField() &&
        has_flag(meta.getPrimaryField()->flags, FieldFlag::AutoIncrement);

    if (!batch_ordered_db_field_names_cache.empty()) {
      model_can_be_inserted_this_pass = true;
      for (const std::string &field_db_name :
           batch_ordered_db_field_names_cache) {
        auto it = model_data_struct.fields_to_write.find(
            QString::fromStdString(field_db_name));
        out_sql_parts.all_values_flattened.append(
            it != model_data_struct.fields_to_write.end() ? it->second
                                                          : QVariant());
        current_model_placeholders_segment.append("?");
      }
      out_sql_parts.row_placeholders.append(
          QString("(%1)").arg(current_model_placeholders_segment.join(",")));
    } else if (is_pure_auto_inc_pk) {
      model_can_be_inserted_this_pass = true;
      if (db_driver_name_upper == "QPSQL") {
        if (models_in_provider_chunk.size() == 1 &&
            model_data_struct.fields_to_write.empty()) {
          // 单个 PG DEFAULT VALUES 不需要显式占位符行
        } else if (models_in_provider_chunk.size() > 1 &&
                   model_data_struct.fields_to_write.empty()) {
          qWarning() << "prepareModelsAndSqlPlaceholders: Unexpected path for "
                        "PG pure auto-inc multi-row batch in SQL prep stage.";
          model_can_be_inserted_this_pass = false;
        }
      } else {
        out_sql_parts.row_placeholders.append("()");
      }
    }

    if (model_can_be_inserted_this_pass) {
      models_prepared_for_sql_build.push_back(model_ptr);
    } else {
      if (first_prepare_error.isOk()) {
        first_prepare_error =
            Error(ErrorCode::MappingError,
                  "Model (table: " + meta.table_name +
                      ", C++ type: " + typeid(*model_ptr).name() +
                      ") could not be prepared for batch insertion (no "
                      "fields/PK to form SQL).");
      }
    }
  }
  return {models_prepared_for_sql_build, first_prepare_error};
}

Error buildFullBatchSqlStatement(
    const Session &session, const QueryBuilder &qb_prototype,
    const ModelMeta &meta,
    const std::vector<std::string> &batch_ordered_db_field_names_cache,
    const OnConflictClause *active_conflict_clause,
    BatchSqlParts &in_out_sql_parts) {
  in_out_sql_parts.can_proceed = false;
  QString db_driver_name_upper = session.getDbHandle().driverName().toUpper();

  QString sql_verb = "INSERT";
  if (active_conflict_clause &&
      active_conflict_clause->action == OnConflictClause::Action::DoNothing) {
    if (db_driver_name_upper.contains("MYSQL"))
      sql_verb = "INSERT IGNORE";
  }

  bool is_pure_auto_inc_pk_case =
      batch_ordered_db_field_names_cache.empty() && meta.getPrimaryField() &&
      has_flag(meta.getPrimaryField()->flags, FieldFlag::AutoIncrement);

  if (is_pure_auto_inc_pk_case) {
    if (db_driver_name_upper == "QPSQL") {
      if (in_out_sql_parts.row_placeholders
              .isEmpty()) { // 应该为空，因为 prepare... 中 PG 单个会跳过
        in_out_sql_parts.sql_insert_base =
            QString("INSERT INTO %1 DEFAULT VALUES")
                .arg(QString::fromStdString(
                    QueryBuilder::quoteSqlIdentifier(meta.table_name)));
        in_out_sql_parts.all_values_flattened.clear();
      } else {
        qWarning() << "buildFullBatchSqlStatement: PG pure auto-inc case "
                      "received non-empty placeholders for single insert.";
        return Error(ErrorCode::StatementPreparationError,
                     "PG pure auto-inc batch SQL build inconsistency for "
                     "single insert.");
      }
    } else {
      if (in_out_sql_parts.row_placeholders.isEmpty()) {
        qWarning() << "buildFullBatchSqlStatement: Non-PG pure auto-inc case "
                      "received empty placeholders.";
        return Error(ErrorCode::StatementPreparationError,
                     "Non-PG pure auto-inc batch SQL build inconsistency.");
      }
      in_out_sql_parts.sql_insert_base =
          QString("%1 INTO %2 () VALUES %3")
              .arg(sql_verb)
              .arg(QString::fromStdString(
                  QueryBuilder::quoteSqlIdentifier(meta.table_name)))
              .arg(in_out_sql_parts.row_placeholders.join(","));
      in_out_sql_parts.all_values_flattened.clear();
    }
  } else if (!batch_ordered_db_field_names_cache.empty() &&
             !in_out_sql_parts.row_placeholders.isEmpty()) {
    QStringList q_fields_for_sql;
    for (const auto &s : batch_ordered_db_field_names_cache)
      q_fields_for_sql.append(
          QString::fromStdString(QueryBuilder::quoteSqlIdentifier(s)));

    in_out_sql_parts.sql_insert_base =
        QString("%1 INTO %2 (%3) VALUES %4")
            .arg(sql_verb)
            .arg(QString::fromStdString(
                QueryBuilder::quoteSqlIdentifier(meta.table_name)))
            .arg(q_fields_for_sql.join(","))
            .arg(in_out_sql_parts.row_placeholders.join(","));
  } else {
    return Error(
        ErrorCode::StatementPreparationError,
        "Cannot build batch INSERT SQL: missing field names or placeholders.");
  }

  in_out_sql_parts.sql_on_conflict_suffix.clear();
  in_out_sql_parts.conflict_suffix_bindings.clear();
  if (active_conflict_clause && !(sql_verb == "INSERT IGNORE" &&
                                  active_conflict_clause->action ==
                                      OnConflictClause::Action::DoNothing)) {
    QueryBuilder temp_qb_for_suffix_build(
        nullptr, qb_prototype.getConnectionName(), &meta);
    temp_qb_for_suffix_build.getState_().on_conflict_clause_ =
        std::make_unique<OnConflictClause>(*active_conflict_clause);
    auto suffix_pair_result = temp_qb_for_suffix_build.buildInsertSQLSuffix(
        batch_ordered_db_field_names_cache);
    in_out_sql_parts.sql_on_conflict_suffix = suffix_pair_result.first;
    in_out_sql_parts.conflict_suffix_bindings = suffix_pair_result.second;
  }

  in_out_sql_parts.final_sql_statement =
      in_out_sql_parts.sql_insert_base + " " +
      in_out_sql_parts.sql_on_conflict_suffix;
  in_out_sql_parts.final_bindings = in_out_sql_parts.all_values_flattened;
  in_out_sql_parts.final_bindings.append(
      in_out_sql_parts.conflict_suffix_bindings);

  bool has_pk_for_returning =
      meta.getPrimaryField() &&
      has_flag(meta.getPrimaryField()->flags, FieldFlag::AutoIncrement) &&
      !meta.getPrimaryField()->db_name.empty();
  bool use_returning =
      (db_driver_name_upper == "QPSQL" && has_pk_for_returning &&
       (!active_conflict_clause ||
        active_conflict_clause->action != OnConflictClause::Action::DoNothing));

  if (use_returning) {
    in_out_sql_parts.final_sql_statement +=
        " RETURNING " + QString::fromStdString(QueryBuilder::quoteSqlIdentifier(
                            meta.getPrimaryField()->db_name));
  }

  in_out_sql_parts.can_proceed = true;
  return make_ok();
}

} // namespace internal_batch_helpers
} // namespace cpporm