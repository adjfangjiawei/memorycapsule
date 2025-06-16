// cpporm/session_migrate_table_ops.cpp
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_migrate_priv.h"

#include <QDebug>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <mutex>

namespace cpporm {
namespace internal {

Error migrateCreateTable(Session &session, const ModelMeta &meta,
                         const QString &driverNameUpper) {
  if (meta.table_name.empty()) {
    return Error(ErrorCode::InvalidConfiguration,
                 "migrateCreateTable: ModelMeta has no table name.");
  }

  std::vector<std::string> column_definitions_sql;
  std::vector<std::string> pk_col_db_names_for_constraint;
  std::vector<std::string> table_constraints_sql;

  for (const auto &field : meta.fields) {
    if (has_flag(field.flags, FieldFlag::Association) ||
        field.db_name.empty()) {
      continue;
    }

    std::string col_def_str = QueryBuilder::quoteSqlIdentifier(field.db_name);
    // Use Session's public static method
    col_def_str += " " + Session::getSqlTypeForCppType(field, driverNameUpper);

    if (has_flag(field.flags, FieldFlag::PrimaryKey)) {
      pk_col_db_names_for_constraint.push_back(field.db_name);
      if ((driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") &&
          has_flag(field.flags, FieldFlag::AutoIncrement)) {
        col_def_str += " AUTO_INCREMENT";
      } else if (driverNameUpper == "QSQLITE" &&
                 has_flag(field.flags, FieldFlag::AutoIncrement) &&
                 pk_col_db_names_for_constraint.size() == 1 &&
                 (field.cpp_type == typeid(int) ||
                  field.cpp_type == typeid(long long) ||
                  field.cpp_type == typeid(unsigned int) ||
                  field.cpp_type == typeid(unsigned long long))) {
        // SQLite: INTEGER PRIMARY KEY AUTOINCREMENT
        // For SQLite, adding "PRIMARY KEY AUTOINCREMENT" to the column
        // definition is one way If we do this, the table-level "PRIMARY KEY
        // (...)" for a single int PK might be redundant or conflict. GORM Go
        // for SQLite with auto-inc integer PK generates: "id" integer primary
        // key autoincrement, It does not add a separate table-level PRIMARY KEY
        // constraint in this specific case. For simplicity and consistency with
        // other DBs that use table-level PK for composite keys, we'll keep the
        // table-level PK constraint and let the column definition just have
        // AUTO_INCREMENT if applicable (MySQL). SQLite's "INTEGER PRIMARY KEY"
        // on a column (typically named id or with ROWID alias) makes it
        // auto-incrementing by default. The AUTOINCREMENT keyword enforces
        // stricter monotonically increasing ID behavior and prevents reuse of
        // IDs. If strict AUTOINCREMENT is desired for SQLite, this would be: if
        // (driverNameUpper == "QSQLITE" && has_flag(field.flags,
        // FieldFlag::AutoIncrement)) col_def_str += " AUTOINCREMENT"; However,
        // this is usually paired with removing it from the table-level PK
        // constraint if it's a single column. Let's keep current logic: MySQL
        // gets col-level AUTO_INCREMENT, others rely on table PK + type.
      }
    }
    if (has_flag(field.flags, FieldFlag::NotNull)) {
      col_def_str += " NOT NULL";
    }
    if (has_flag(field.flags, FieldFlag::Unique) &&
        !has_flag(field.flags, FieldFlag::PrimaryKey)) {
      col_def_str += " UNIQUE";
    }
    column_definitions_sql.push_back(col_def_str);
  }

  if (!pk_col_db_names_for_constraint.empty()) {
    std::string pk_constraint_str = "PRIMARY KEY (";
    for (size_t i = 0; i < pk_col_db_names_for_constraint.size(); ++i) {
      pk_constraint_str +=
          QueryBuilder::quoteSqlIdentifier(pk_col_db_names_for_constraint[i]);
      if (i < pk_col_db_names_for_constraint.size() - 1)
        pk_constraint_str += ", ";
    }
    pk_constraint_str += ")";
    table_constraints_sql.push_back(pk_constraint_str);
  }

  for (const auto &assoc : meta.associations) {
    if (assoc.type == AssociationType::BelongsTo &&
        !assoc.foreign_key_db_name.empty()) {
      std::string fk_col_on_curr_table = assoc.foreign_key_db_name;
      std::string target_table_name_str;
      std::string target_pk_col_name_str;

      cpporm::internal::ModelFactory factory_fn;
      {
        std::lock_guard<std::mutex> lock(
            cpporm::internal::getGlobalModelFactoryRegistryMutex());
        auto it_factory =
            cpporm::internal::getGlobalModelFactoryRegistry().find(
                assoc.target_model_type);
        if (it_factory !=
            cpporm::internal::getGlobalModelFactoryRegistry().end()) {
          factory_fn = it_factory->second;
        }
      }
      if (factory_fn) {
        auto temp_target_model = factory_fn();
        if (temp_target_model) {
          const ModelMeta &target_meta = temp_target_model->_getOwnModelMeta();
          target_table_name_str = target_meta.table_name;
          if (!assoc.target_model_pk_db_name.empty()) {
            target_pk_col_name_str = assoc.target_model_pk_db_name;
          } else if (!target_meta.primary_keys_db_names.empty()) {
            target_pk_col_name_str = target_meta.primary_keys_db_names[0];
          }
        }
      }

      if (!target_table_name_str.empty() && !target_pk_col_name_str.empty()) {
        std::string fk_sql =
            "FOREIGN KEY (" +
            QueryBuilder::quoteSqlIdentifier(fk_col_on_curr_table) + ")" +
            " REFERENCES " +
            QueryBuilder::quoteSqlIdentifier(target_table_name_str) + " (" +
            QueryBuilder::quoteSqlIdentifier(target_pk_col_name_str) + ")";
        table_constraints_sql.push_back(fk_sql);
      } else {
        qWarning() << "migrateCreateTable (FK): Could not determine target "
                      "table/PK for BelongsTo association '"
                   << QString::fromStdString(assoc.cpp_field_name)
                   << "' on table '" << QString::fromStdString(meta.table_name)
                   << "'. FK constraint not created.";
      }
    }
  }

  std::string all_definitions_sql_str;
  for (size_t i = 0; i < column_definitions_sql.size(); ++i) {
    all_definitions_sql_str += column_definitions_sql[i];
    if (i < column_definitions_sql.size() - 1 ||
        !table_constraints_sql.empty()) {
      all_definitions_sql_str += ", ";
    }
  }
  for (size_t i = 0; i < table_constraints_sql.size(); ++i) {
    all_definitions_sql_str += table_constraints_sql[i];
    if (i < table_constraints_sql.size() - 1) {
      all_definitions_sql_str += ", ";
    }
  }

  if (!all_definitions_sql_str.empty()) {
    bool ends_with_comma_space = true;
    while (ends_with_comma_space && !all_definitions_sql_str.empty()) {
      if (all_definitions_sql_str.back() == ',' ||
          all_definitions_sql_str.back() == ' ') {
        all_definitions_sql_str.pop_back();
      } else {
        ends_with_comma_space = false;
      }
    }
  }
  if (all_definitions_sql_str.empty()) {
    return Error(ErrorCode::InvalidConfiguration,
                 "migrateCreateTable: No column definitions or constraints "
                 "generated for table '" +
                     meta.table_name + "'.");
  }

  QString create_table_ddl =
      QString("CREATE TABLE IF NOT EXISTS %1 (%2);")
          .arg(QString::fromStdString(
              QueryBuilder::quoteSqlIdentifier(meta.table_name)))
          .arg(QString::fromStdString(all_definitions_sql_str));

  qInfo() << "migrateCreateTable (DDL for "
          << QString::fromStdString(meta.table_name)
          << "): " << create_table_ddl;
  // Pass session.getDbHandle() which returns a QSqlDatabase copy
  auto [_, err_obj] =
      execute_ddl_query(session.getDbHandle(), create_table_ddl);
  return err_obj;
}

std::pair<QSqlQuery, Error>
execute_ddl_query(QSqlDatabase db,
                  const QString &ddl_sql) { // Takes QSqlDatabase by value
  if (!db.isOpen()) {
    if (!db.open()) {
      QSqlError err = db.lastError();
      qWarning() << "execute_ddl_query: Failed to open database for DDL:"
                 << err.text() << "SQL:" << ddl_sql;
      return {QSqlQuery(db), Error(ErrorCode::ConnectionNotOpen,
                                   "Failed to open database for DDL: " +
                                       err.text().toStdString())};
    }
  }
  QSqlQuery query(db);
  if (!query.exec(ddl_sql)) {
    QSqlError err = query.lastError();
    qWarning() << "execute_ddl_query: DDL execution failed:" << err.text()
               << "SQL:" << ddl_sql;
    return {query, Error(ErrorCode::QueryExecutionError,
                         "DDL execution failed: " + err.text().toStdString() +
                             " SQL: " + ddl_sql.toStdString(),
                         err.nativeErrorCode().toInt())};
  }
  return {query, make_ok()};
}

} // namespace internal
} // namespace cpporm