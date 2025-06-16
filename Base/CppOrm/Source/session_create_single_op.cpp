// cpporm/session_create_single_op.cpp
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
// #include "cpporm/qt_db_manager.h" // 如果需要直接访问

#include <QDateTime>
#include <QDebug>
#include <QMetaType>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

namespace cpporm {

// Session 的 IQueryExecutor::CreateImpl 实现 (单个模型创建)
std::expected<QVariant, Error>
Session::CreateImpl(const QueryBuilder &qb, ModelBase &model_instance,
                    const OnConflictClause *conflict_options_override) {

  const OnConflictClause *active_conflict_clause = conflict_options_override;
  if (!active_conflict_clause && qb.getOnConflictClause()) {
    active_conflict_clause = qb.getOnConflictClause();
  }
  if (!active_conflict_clause && temp_on_conflict_clause_) {
    active_conflict_clause = temp_on_conflict_clause_.get();
  }

  bool clear_temp_on_conflict_at_end =
      (active_conflict_clause == temp_on_conflict_clause_.get() &&
       !conflict_options_override && !qb.getOnConflictClause());

  const ModelMeta *meta_ptr = qb.getModelMeta();
  if (!meta_ptr) { // 尝试从模型实例获取
    meta_ptr = &(model_instance._getOwnModelMeta());
  }
  if (!meta_ptr || meta_ptr->table_name.empty()) {
    if (clear_temp_on_conflict_at_end)
      this->clearTempOnConflictClause();
    return std::unexpected(
        Error(ErrorCode::InvalidConfiguration,
              "CreateImpl: ModelMeta is not valid or table name is empty."));
  }
  const ModelMeta &meta = *meta_ptr;

  Error hook_err = model_instance.beforeCreate(*this);
  if (hook_err) {
    if (clear_temp_on_conflict_at_end)
      this->clearTempOnConflictClause();
    return std::unexpected(hook_err);
  }

  this->autoSetTimestamps(model_instance, meta, true);
  cpporm::internal::SessionModelDataForWrite data_to_write =
      this->extractModelData(model_instance, meta, false /* for_update */,
                             true /* include_timestamps_even_if_null */);

  if (data_to_write.fields_to_write.empty() &&
      !data_to_write.has_auto_increment_pk) {
    bool is_simple_auto_inc_model =
        data_to_write.has_auto_increment_pk && meta.fields.size() == 1 &&
        meta.getPrimaryField() &&
        has_flag(meta.getPrimaryField()->flags, FieldFlag::AutoIncrement);
    if (!is_simple_auto_inc_model) {
      if (clear_temp_on_conflict_at_end)
        this->clearTempOnConflictClause();
      return std::unexpected(Error(ErrorCode::MappingError,
                                   "No fields to insert for Create operation "
                                   "and not a simple auto-increment model."));
    }
  }

  QStringList field_names_qsl;
  QVariantList values_to_bind_insert;
  QStringList placeholders_qsl;
  std::vector<std::string> ordered_db_field_names_vec;

  QString driverNameUpper = db_handle_.driverName().toUpper();

  for (const auto &[db_name_qstr, q_val] : data_to_write.fields_to_write) {
    ordered_db_field_names_vec.push_back(db_name_qstr.toStdString());
    field_names_qsl.append(QString::fromStdString(
        QueryBuilder::quoteSqlIdentifier(db_name_qstr.toStdString())));
    values_to_bind_insert.append(q_val); // Value always gets appended

    bool placeholder_handled = false;
    if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
      const FieldMeta *fm = meta.findFieldByDbName(db_name_qstr.toStdString());
      if (fm &&
          (fm->db_type_hint == "POINT" || fm->db_type_hint == "GEOMETRY" ||
           fm->db_type_hint == "LINESTRING" || fm->db_type_hint == "POLYGON" ||
           fm->db_type_hint == "MULTIPOINT" ||
           fm->db_type_hint == "MULTILINESTRING" ||
           fm->db_type_hint == "MULTIPOLYGON" ||
           fm->db_type_hint == "GEOMETRYCOLLECTION")) {
        placeholders_qsl.append("ST_GeomFromText(?)");
        placeholder_handled = true;
      }
    }
    if (!placeholder_handled) {
      placeholders_qsl.append("?");
    }
  }

  QString sql_verb = "INSERT";
  if (active_conflict_clause &&
      active_conflict_clause->action == OnConflictClause::Action::DoNothing) {
    if (db_handle_.driverName().toUpper().contains("MYSQL")) {
      sql_verb = "INSERT IGNORE";
    }
    // 对于 PostgreSQL, ON CONFLICT DO NOTHING 会在 suffix 中处理
  }

  QString sql_query_base;
  if (!field_names_qsl.isEmpty()) {
    sql_query_base = QString("%1 INTO %2 (%3) VALUES (%4)")
                         .arg(sql_verb)
                         .arg(QString::fromStdString(
                             QueryBuilder::quoteSqlIdentifier(meta.table_name)))
                         .arg(field_names_qsl.join(", "))
                         .arg(placeholders_qsl.join(", "));
  } else if (data_to_write.has_auto_increment_pk) { // 只有自增主键的情况
    if (db_handle_.driverName().toUpper() == "QPSQL") {
      // PostgreSQL: INSERT INTO "table" DEFAULT VALUES
      // 如果是 INSERT IGNORE，PG 的对应是 ON CONFLICT DO NOTHING，由 suffix
      // 处理
      sql_query_base =
          QString("INSERT INTO %1 DEFAULT VALUES") // sql_verb 总是 INSERT
              .arg(QString::fromStdString(
                  QueryBuilder::quoteSqlIdentifier(meta.table_name)));
    } else { // MySQL: INSERT IGNORE INTO `table` () VALUES () 或 INSERT INTO
             // `table` () VALUES ()
      sql_query_base =
          QString("%1 INTO %2 () VALUES ()")
              .arg(sql_verb)
              .arg(QString::fromStdString(
                  QueryBuilder::quoteSqlIdentifier(meta.table_name)));
    }
  } else {
    if (clear_temp_on_conflict_at_end)
      this->clearTempOnConflictClause();
    return std::unexpected(
        Error(ErrorCode::MappingError,
              "Cannot construct INSERT: no fields and no auto-inc PK."));
  }

  QString sql_on_conflict_suffix_str;
  QVariantList suffix_bindings;
  if (active_conflict_clause) { // 只要有 active_conflict_clause 就尝试构建后缀
    // 创建一个临时的 QB 来构建后缀，它不需要 executor
    QueryBuilder temp_qb_for_suffix(nullptr, this->connection_name_, &meta);
    // 复制 OnConflictClause 的状态给临时 QB
    temp_qb_for_suffix.getState_().on_conflict_clause_ =
        std::make_unique<OnConflictClause>(*active_conflict_clause);

    auto suffix_pair =
        temp_qb_for_suffix.buildInsertSQLSuffix(ordered_db_field_names_vec);
    sql_on_conflict_suffix_str = suffix_pair.first;
    suffix_bindings = suffix_pair.second;

    // 特殊处理 MySQL 的 INSERT IGNORE：如果 buildInsertSQLSuffix 返回了非空后缀
    // （例如，它内部没有处理 IGNORE 的特殊情况），而我们已经设置了 sql_verb =
    // "INSERT IGNORE"， 这时需要避免重复的冲突处理。 但当前的
    // buildInsertSQLSuffix 对于 Action::DoNothing (MySQL) 返回空，所以没问题。
  }

  QString final_sql_query_str = sql_query_base + sql_on_conflict_suffix_str;
  QVariantList all_bindings = values_to_bind_insert;
  all_bindings.append(suffix_bindings);

  bool driver_can_return_id =
      db_handle_.driver()->hasFeature(QSqlDriver::LastInsertId);
  bool use_returning_clause =
      (db_handle_.driverName().toUpper() == "QPSQL" &&
       data_to_write.has_auto_increment_pk &&
       !data_to_write.auto_increment_pk_name_db.isEmpty() &&
       // RETURNING 适用于普通 INSERT 或 ON CONFLICT ... DO UPDATE
       (sql_verb ==
            "INSERT" || // sql_verb 对 PG 总是 INSERT，冲突由 suffix 处理
        (active_conflict_clause && active_conflict_clause->action !=
                                       OnConflictClause::Action::DoNothing)));

  if (use_returning_clause) {
    final_sql_query_str +=
        " RETURNING " +
        QString::fromStdString(QueryBuilder::quoteSqlIdentifier(
            data_to_write.auto_increment_pk_name_db.toStdString()));
  }

  auto [query, exec_err] = execute_query_internal(
      this->db_handle_, final_sql_query_str, all_bindings);

  if (clear_temp_on_conflict_at_end)
    this->clearTempOnConflictClause();

  if (exec_err)
    return std::unexpected(exec_err);

  long long rows_affected = query.numRowsAffected();
  model_instance._is_persisted =
      (rows_affected > 0 ||
       (active_conflict_clause &&
        active_conflict_clause->action != OnConflictClause::Action::DoNothing &&
        rows_affected >=
            0)); // MySQL ON DUP UPDATE 可能返回 0（无变化）或 2（有变化）

  QVariant returned_id;
  bool was_insert_action =
      (sql_verb == "INSERT" || sql_verb == "INSERT IGNORE");
  bool was_upsert_action =
      (active_conflict_clause &&
       active_conflict_clause->action != OnConflictClause::Action::DoNothing);

  if (use_returning_clause && (was_insert_action || was_upsert_action) &&
      rows_affected > 0) {
    if (query.next())
      returned_id = query.value(0);
  } else if (data_to_write.has_auto_increment_pk && driver_can_return_id &&
             was_insert_action && rows_affected == 1) {
    returned_id = query.lastInsertId();
  } else if (data_to_write.has_auto_increment_pk && driver_can_return_id &&
             was_upsert_action && rows_affected > 0 && sql_verb == "INSERT") {
    // 对于 MySQL 的 ON DUPLICATE KEY UPDATE, 如果实际执行了 UPDATE,
    // lastInsertId 通常是0或旧ID 如果是新插入（即冲突导致了新行），lastInsertId
    // 是新ID，rows_affected 是1 如果是更新，rows_affected 是2 (MySQL 5.x+) 或 1
    // (如果行未改变)
    if (rows_affected == 1 &&
        db_handle_.driverName().toUpper().contains("MYSQL")) { // 假设是新插入
      returned_id = query.lastInsertId();
    }
  }

  if (returned_id.isValid() && !returned_id.isNull() &&
      data_to_write.has_auto_increment_pk) {
    std::any pk_val_any;
    bool conversion_ok = false;
    const auto &pk_type = data_to_write.pk_cpp_type_for_autoincrement;
    const std::string &pk_cpp_name =
        data_to_write.pk_cpp_name_for_autoincrement;
    if (pk_type == typeid(int))
      pk_val_any = returned_id.toInt(&conversion_ok);
    else if (pk_type == typeid(long long))
      pk_val_any = returned_id.toLongLong(&conversion_ok);
    else if (pk_type == typeid(unsigned int))
      pk_val_any = returned_id.toUInt(&conversion_ok);
    else if (pk_type == typeid(unsigned long long))
      pk_val_any = returned_id.toULongLong(&conversion_ok);
    else if (pk_type == typeid(std::string)) {
      pk_val_any = returned_id.toString().toStdString();
      conversion_ok = true;
    } else {
      qWarning() << "CreateImpl: Unhandled PK C++ type for backfill:"
                 << pk_type.name();
    }
    if (conversion_ok) {
      Error set_pk_err = model_instance.setFieldValue(pk_cpp_name, pk_val_any);
      if (set_pk_err)
        qWarning() << "CreateImpl: Error setting auto-incremented PK: "
                   << set_pk_err.toString().c_str();
    } else {
      qWarning() << "CreateImpl: Conversion failed for PK backfill. DB val:"
                 << returned_id.toString() << " to C++ type " << pk_type.name();
    }
  }

  if (model_instance._is_persisted) {
    hook_err = model_instance.afterCreate(*this);
    if (hook_err)
      return std::unexpected(hook_err);
  }

  if (returned_id.isValid() && !returned_id.isNull())
    return returned_id;
  return QVariant(rows_affected);
}

// Session 的便捷 Create 方法
std::expected<QVariant, Error>
Session::Create(ModelBase &model,
                const OnConflictClause *conflict_options_override) {
  // 使用 model 的元数据创建一个基础的 QueryBuilder，它将包含正确的 executor
  QueryBuilder qb = this->Model(&model);
  return this->CreateImpl(qb, model, conflict_options_override);
}

} // namespace cpporm