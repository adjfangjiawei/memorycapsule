#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_migrate_priv.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <algorithm>

namespace cpporm {
namespace internal {

std::string normalizeDbType(const std::string &db_type_raw,
                            const QString &driverNameUpper) {
  std::string lower_type = db_type_raw;
  std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
    if (lower_type.rfind("int", 0) == 0 &&
        lower_type.find("unsigned") == std::string::npos &&
        lower_type != "tinyint(1)")
      return "int";
    if (lower_type.rfind("int unsigned", 0) == 0)
      return "int unsigned";
    if (lower_type.rfind("bigint", 0) == 0 &&
        lower_type.find("unsigned") == std::string::npos)
      return "bigint";
    if (lower_type.rfind("bigint unsigned", 0) == 0)
      return "bigint unsigned";
    if (lower_type == "tinyint(1)")
      return "boolean";
    if (lower_type.rfind("varchar", 0) == 0)
      return "varchar";
    if (lower_type.rfind("char", 0) == 0 &&
        lower_type.find("varchar") == std::string::npos)
      return "char";
    if (lower_type == "text" || lower_type == "tinytext" ||
        lower_type == "mediumtext" || lower_type == "longtext")
      return "text";
    if (lower_type == "datetime")
      return "datetime";
    if (lower_type == "timestamp")
      return "timestamp";
    if (lower_type == "date")
      return "date";
    if (lower_type == "time")
      return "time";
    if (lower_type == "float")
      return "float";
    if (lower_type == "double" || lower_type == "real")
      return "double";
    if (lower_type.rfind("decimal", 0) == 0 ||
        lower_type.rfind("numeric", 0) == 0)
      return "decimal";
    if (lower_type == "blob" || lower_type == "tinyblob" ||
        lower_type == "mediumblob" || lower_type == "longblob" ||
        lower_type == "varbinary" || lower_type == "binary")
      return "blob";
  } else if (driverNameUpper == "QPSQL") {
    if (lower_type == "integer" || lower_type == "int4")
      return "int";
    if (lower_type == "bigint" || lower_type == "int8")
      return "bigint";
    if (lower_type == "smallint" || lower_type == "int2")
      return "smallint";
    if (lower_type == "boolean" || lower_type == "bool")
      return "boolean";
    if (lower_type.rfind("character varying", 0) == 0)
      return "varchar";
    if ((lower_type.rfind("character(", 0) == 0 ||
         lower_type.rfind("char(", 0) == 0) &&
        lower_type.find("varying") == std::string::npos)
      return "char";
    if (lower_type == "text")
      return "text";
    if (lower_type == "timestamp without time zone" ||
        lower_type == "timestamp")
      return "timestamp";
    if (lower_type == "timestamp with time zone")
      return "timestamptz";
    if (lower_type == "date")
      return "date";
    if (lower_type == "time without time zone" || lower_type == "time")
      return "time";
    if (lower_type == "time with time zone")
      return "timetz";
    if (lower_type == "real" || lower_type == "float4")
      return "float";
    if (lower_type == "double precision" || lower_type == "float8")
      return "double";
    if (lower_type == "numeric" || lower_type == "decimal")
      return "decimal";
    if (lower_type == "bytea")
      return "blob";
  } else if (driverNameUpper == "QSQLITE") {
    if (lower_type.find("int") != std::string::npos)
      return "int"; // Covers INTEGER
    if (lower_type == "text" || lower_type.find("char") != std::string::npos ||
        lower_type.find("clob") != std::string::npos)
      return "text"; // Covers VARCHAR, TEXT etc.
    if (lower_type == "blob" || lower_type.empty())
      return "blob"; // Covers BLOB
    if (lower_type == "real" || lower_type.find("floa") != std::string::npos ||
        lower_type.find("doub") != std::string::npos)
      return "double"; // Covers REAL, FLOAT, DOUBLE
    if (lower_type == "numeric" ||
        lower_type.find("deci") != std::string::npos ||
        lower_type.find("bool") != std::string::npos ||
        lower_type.find("date") != std::string::npos ||
        lower_type.find("datetime") != std::string::npos)
      return "numeric"; // Covers NUMERIC, DECIMAL, BOOLEAN, DATE, DATETIME
  }
  return lower_type; // Return normalized if not specifically mapped, or
                     // original lowercased
}

std::map<std::string, DbColumnInfo>
getTableColumnsInfo(Session &session, const QString &tableName,
                    const QString &driverNameUpper) {
  std::map<std::string, DbColumnInfo> columns;
  QSqlQuery query(session.getDbHandle());
  QString sql;

  if (driverNameUpper == "QSQLITE") {
    sql = QString("PRAGMA table_xinfo(%1);").arg(tableName);
    if (!query.exec(sql)) {
      sql = QString("PRAGMA table_info(%1);").arg(tableName);
      if (!query.exec(sql)) {
        qWarning() << "getTableColumnsInfo (SQLite): Failed to query PRAGMA "
                      "table_info/table_xinfo for table"
                   << tableName << ":" << query.lastError().text();
        return columns;
      }
    }
  } else if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
    sql = QString("SHOW FULL COLUMNS FROM %1;")
              .arg(QString::fromStdString(
                  QueryBuilder::quoteSqlIdentifier(tableName.toStdString())));
    if (!query.exec(sql)) {
      qWarning() << "getTableColumnsInfo (MySQL): Failed to query SHOW FULL "
                    "COLUMNS for table"
                 << tableName << ":" << query.lastError().text()
                 << "SQL:" << sql;
      return columns;
    }
  } else if (driverNameUpper == "QPSQL") {
    sql = QString("SELECT column_name, data_type, udt_name, is_nullable, "
                  "column_default, "
                  "character_maximum_length, numeric_precision, numeric_scale, "
                  "collation_name "
                  "FROM information_schema.columns WHERE table_schema = "
                  "current_schema() AND table_name = '%1';")
              .arg(tableName);
    if (!query.exec(sql)) {
      qWarning() << "getTableColumnsInfo (PostgreSQL): Failed to query "
                    "information_schema.columns for table"
                 << tableName << ":" << query.lastError().text()
                 << "SQL:" << sql;
      return columns;
    }
  } else {
    qWarning()
        << "getTableColumnsInfo: Unsupported driver for detailed column info:"
        << driverNameUpper;
    return columns;
  }

  while (query.next()) {
    DbColumnInfo colInfo;
    if (driverNameUpper == "QSQLITE") {
      colInfo.name = query.value("name").toString().toStdString();
      colInfo.type = query.value("type").toString().toStdString();
      colInfo.is_nullable = !query.value("notnull").toBool();
      QVariant dflt_val = query.value("dflt_value");
      colInfo.default_value =
          dflt_val.isNull() ? "" : dflt_val.toString().toStdString();
      colInfo.column_key = query.value("pk").toInt() > 0 ? "PRI" : "";
    } else if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
      colInfo.name = query.value("Field").toString().toStdString();
      colInfo.type = query.value("Type").toString().toStdString();
      colInfo.is_nullable = (query.value("Null").toString().toUpper() == "YES");
      QVariant defVal = query.value("Default");
      colInfo.default_value =
          defVal.isNull() ? "" : defVal.toString().toStdString();
      colInfo.collation_name =
          query.value("Collation").toString().toStdString();
      colInfo.column_key = query.value("Key").toString().toStdString();
      colInfo.extra = query.value("Extra").toString().toStdString();
    } else if (driverNameUpper == "QPSQL") {
      colInfo.name = query.value("column_name").toString().toStdString();
      std::string pg_udt_name =
          query.value("udt_name").toString().toStdString();
      std::string pg_data_type =
          query.value("data_type").toString().toStdString();

      if (pg_data_type.rfind("ARRAY", 0) == 0) {
        colInfo.type = pg_udt_name;
        if (colInfo.type.rfind('_', 0) == 0)
          colInfo.type.erase(0, 1);
        colInfo.type += "[]";
      } else if (pg_udt_name.empty() || pg_udt_name == "anyelement" ||
                 pg_udt_name == "anyarray") {
        colInfo.type = pg_data_type;
      } else {
        colInfo.type = pg_udt_name;
      }
      colInfo.is_nullable =
          (query.value("is_nullable").toString().toUpper() == "YES");
      colInfo.default_value =
          query.value("column_default").toString().toStdString();
      colInfo.collation_name =
          query.value("collation_name").toString().toStdString();
    }
    if (!colInfo.name.empty()) {
      colInfo.normalized_type = normalizeDbType(colInfo.type, driverNameUpper);
      columns[colInfo.name] = colInfo;
    }
  }
  return columns;
}

Error migrateModifyColumns(Session &session, const ModelMeta &meta,
                           const QString &driverNameUpper) {
  qInfo() << "migrateModifyColumns: Checking columns for table '"
          << QString::fromStdString(meta.table_name) << "'...";
  std::map<std::string, DbColumnInfo> existing_db_columns = getTableColumnsInfo(
      session, QString::fromStdString(meta.table_name), driverNameUpper);

  for (const auto &model_field : meta.fields) {
    if (has_flag(model_field.flags, FieldFlag::Association) ||
        model_field.db_name.empty()) {
      continue;
    }

    std::string model_sql_type_str =
        Session::getSqlTypeForCppType(model_field, driverNameUpper);
    std::string model_normalized_sql_type =
        normalizeDbType(model_sql_type_str, driverNameUpper);

    auto it_db_col = existing_db_columns.find(model_field.db_name);
    if (it_db_col == existing_db_columns.end()) { // Column doesn't exist in DB
      qInfo() << "migrateModifyColumns: Column '"
              << QString::fromStdString(model_field.db_name)
              << "' not found in table '"
              << QString::fromStdString(meta.table_name)
              << "'. Attempting to ADD.";

      std::string add_col_sql_str =
          "ALTER TABLE " + QueryBuilder::quoteSqlIdentifier(meta.table_name) +
          " ADD COLUMN " +
          QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " " +
          model_sql_type_str;

      if (has_flag(model_field.flags, FieldFlag::NotNull))
        add_col_sql_str += " NOT NULL";
      if (has_flag(model_field.flags, FieldFlag::Unique) &&
          !has_flag(model_field.flags, FieldFlag::PrimaryKey)) {
        add_col_sql_str += " UNIQUE";
      }
      add_col_sql_str += ";";

      qInfo() << "migrateModifyColumns (ADD DDL): "
              << QString::fromStdString(add_col_sql_str);
      auto [_, add_err] = execute_ddl_query(
          session.getDbHandle(), QString::fromStdString(add_col_sql_str));
      if (add_err) {
        qWarning() << "migrateModifyColumns: Failed to ADD column '"
                   << QString::fromStdString(model_field.db_name)
                   << "': " << QString::fromStdString(add_err.toString());
      }

    } else { // Column exists, check for modifications
      DbColumnInfo &db_col = it_db_col->second;
      bool needs_alter = false;

      // Check for type differences
      if (model_normalized_sql_type != db_col.normalized_type) {
        needs_alter = true; // Basic normalized types differ

        // SQLite specific handling for TEXT/VARCHAR compatibility
        if (driverNameUpper == "QSQLITE" &&
            ((model_normalized_sql_type == "text" &&
              db_col.normalized_type == "varchar") ||
             (model_normalized_sql_type == "varchar" &&
              db_col.normalized_type == "text"))) {
          needs_alter = false; // Treat as compatible for SQLite
        }
        // Skip alter for narrowing integer conversions
        else if ((model_normalized_sql_type == "int" &&
                  db_col.normalized_type == "bigint") ||
                 (model_normalized_sql_type == "smallint" &&
                  (db_col.normalized_type == "int" ||
                   db_col.normalized_type == "bigint"))
                 // Consider adding unsigned variations if needed
        ) {
          qInfo() << "migrateModifyColumns: Model requests narrowing integer "
                     "conversion for column '"
                  << QString::fromStdString(model_field.db_name)
                  << "' from DB type '" << QString::fromStdString(db_col.type)
                  << "' to model type '"
                  << QString::fromStdString(model_sql_type_str)
                  << "'. Skipping automatic type alteration.";
          needs_alter = false;
        }
      } else { // Normalized types are the same, check for other differences
               // like VARCHAR length if desired
        // For VARCHAR, if model_sql_type_str (e.g. "VARCHAR(1000)") differs
        // from db_col.type (e.g. "varchar(255)"), it implies a length change.
        // We generally want to allow widening. This comparison is very basic
        // and assumes model_sql_type_str is the canonical definition.
        if (model_normalized_sql_type == "varchar" &&
            model_sql_type_str != db_col.type) {
          // A more robust check would parse lengths. For now, if the raw types
          // differ but normalized are same (varchar), assume alter for
          // widening. Example: model "VARCHAR(1000)", db "varchar(255)" ->
          // needs_alter = true. Example: model "VARCHAR(50)", db "varchar(255)"
          // -> this is narrowing, more complex. GORM often allows widening but
          // might be cautious with narrowing VARCHAR. Let's assume for now if
          // raw types differ for varchar, attempt an alter. This could be
          // refined by parsing lengths from model_sql_type_str and db_col.type.
          // needs_alter = true; // Cautiously enable this if length parsing
          // isn't in place For now, let's say if normalized is same, type is
          // compatible. A change from TEXT to VARCHAR(1000) will be caught by
          // normalized_type diff.
        }
      }

      if (needs_alter) {
        qInfo() << "migrateModifyColumns: Type mismatch or desired change for "
                   "column '"
                << QString::fromStdString(model_field.db_name)
                << "'. DB type: '" << QString::fromStdString(db_col.type)
                << "' (norm: " << QString::fromStdString(db_col.normalized_type)
                << "), Model type: '"
                << QString::fromStdString(model_sql_type_str) << "' (norm: "
                << QString::fromStdString(model_normalized_sql_type)
                << "). Attempting to MODIFY.";

        std::string alter_col_sql_str =
            "ALTER TABLE " + QueryBuilder::quoteSqlIdentifier(meta.table_name);
        if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
          alter_col_sql_str +=
              " MODIFY COLUMN " +
              QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " " +
              model_sql_type_str;
          if (has_flag(model_field.flags, FieldFlag::NotNull))
            alter_col_sql_str += " NOT NULL";
          else
            alter_col_sql_str += " NULL";
        } else if (driverNameUpper == "QPSQL") {
          alter_col_sql_str +=
              " ALTER COLUMN " +
              QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " TYPE " +
              model_sql_type_str;
          // For PG, nullability and default changes are separate ALTER
          // statements. This simplified version only handles type change. A
          // full solution would chain them.
        } else if (driverNameUpper == "QSQLITE") {
          qWarning()
              << "migrateModifyColumns: SQLite has very limited ALTER TABLE "
                 "support for modifying column types. Type change for '"
              << QString::fromStdString(model_field.db_name) << "' skipped.";
          continue; // Skip to next field
        } else {
          qWarning() << "migrateModifyColumns: Don't know how to alter column "
                        "type for driver "
                     << driverNameUpper << ". Column '"
                     << QString::fromStdString(model_field.db_name)
                     << "' type alteration skipped.";
          continue; // Skip to next field
        }
        alter_col_sql_str += ";";

        qInfo() << "migrateModifyColumns (MODIFY DDL): "
                << QString::fromStdString(alter_col_sql_str);
        auto [_, alter_err] = execute_ddl_query(
            session.getDbHandle(), QString::fromStdString(alter_col_sql_str));
        if (alter_err) {
          qWarning() << "migrateModifyColumns: Failed to MODIFY column '"
                     << QString::fromStdString(model_field.db_name)
                     << "': " << QString::fromStdString(alter_err.toString());
        }
      }
      // TODO: Add logic to check and alter NULLability, DEFAULT values if they
      // differ, which might require separate ALTER COLUMN statements for some
      // DBs (like PostgreSQL).
    }
  }
  return make_ok();
}

} // namespace internal
} // namespace cpporm