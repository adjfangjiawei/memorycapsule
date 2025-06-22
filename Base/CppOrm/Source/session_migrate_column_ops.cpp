// Base/CppOrm/Source/session_migrate_column_ops.cpp
#include <QDebug>
#include <QString>    // For QString::fromStdString
#include <algorithm>  // For std::transform
#include <cctype>     // For ::toupper

#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_migrate_priv.h"
#include "cpporm_sqldriver/sql_database.h"
#include "cpporm_sqldriver/sql_field.h"
#include "cpporm_sqldriver/sql_query.h"
#include "cpporm_sqldriver/sql_record.h"

namespace cpporm {
    namespace internal {

        std::string normalizeDbType(const std::string &db_type_raw, const QString &driverNameUpper);

        std::map<std::string, DbColumnInfo> getTableColumnsInfo(Session &session, const QString &tableNameQString, const QString &driverNameUpper) {
            std::map<std::string, DbColumnInfo> columns;
            cpporm_sqldriver::SqlQuery query(session.getDbHandle());
            std::string sql_std;
            std::string tableNameStd = tableNameQString.toStdString();

            if (driverNameUpper == "QSQLITE") {
                sql_std = "PRAGMA table_xinfo(" + QueryBuilder::quoteSqlIdentifier(tableNameStd) + ");";
                if (!query.exec(sql_std)) {
                    sql_std = "PRAGMA table_info(" + QueryBuilder::quoteSqlIdentifier(tableNameStd) + ");";
                    if (!query.exec(sql_std)) {
                        qWarning() << "getTableColumnsInfo (SQLite): Failed to query PRAGMA "
                                      "table_info/table_xinfo for table"
                                   << tableNameQString << ":" << QString::fromStdString(query.lastError().text());
                        return columns;
                    }
                }
            } else if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                sql_std = "SHOW FULL COLUMNS FROM " + QueryBuilder::quoteSqlIdentifier(tableNameStd) + ";";
                if (!query.exec(sql_std)) {
                    qWarning() << "getTableColumnsInfo (MySQL): Failed to query SHOW FULL "
                                  "COLUMNS for table"
                               << tableNameQString << ":" << QString::fromStdString(query.lastError().text()) << "SQL:" << QString::fromStdString(sql_std);
                    return columns;
                }
            } else if (driverNameUpper == "QPSQL") {
                sql_std =
                    "SELECT column_name, data_type, udt_name, is_nullable, "
                    "column_default, "
                    "character_maximum_length, numeric_precision, numeric_scale, "
                    "collation_name "
                    "FROM information_schema.columns WHERE table_schema = "
                    "current_schema() AND table_name = '" +
                    tableNameStd + "';";
                if (!query.exec(sql_std)) {
                    qWarning() << "getTableColumnsInfo (PostgreSQL): Failed to query "
                                  "information_schema.columns for table"
                               << tableNameQString << ":" << QString::fromStdString(query.lastError().text()) << "SQL:" << QString::fromStdString(sql_std);
                    return columns;
                }
            } else {
                qWarning() << "getTableColumnsInfo: Unsupported driver for detailed column info:" << driverNameUpper;
                return columns;
            }

            cpporm_sqldriver::SqlRecord rec_meta = query.recordMetadata();
            while (query.next()) {
                DbColumnInfo colInfo;
                if (driverNameUpper == "QSQLITE") {
                    colInfo.name = query.value(rec_meta.indexOf("name")).toString();
                    colInfo.type = query.value(rec_meta.indexOf("type")).toString();
                    colInfo.is_nullable = !query.value(rec_meta.indexOf("notnull")).toBool();
                    cpporm_sqldriver::SqlValue dflt_val_sv = query.value(rec_meta.indexOf("dflt_value"));
                    colInfo.default_value = dflt_val_sv.isNull() ? "" : dflt_val_sv.toString();
                    colInfo.column_key = query.value(rec_meta.indexOf("pk")).toInt32() > 0 ? "PRI" : "";
                } else if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                    colInfo.name = query.value(rec_meta.indexOf("Field")).toString();
                    colInfo.type = query.value(rec_meta.indexOf("Type")).toString();
                    std::string nullable_str = query.value(rec_meta.indexOf("Null")).toString();
                    std::transform(nullable_str.begin(), nullable_str.end(), nullable_str.begin(), ::toupper);
                    colInfo.is_nullable = (nullable_str == "YES");
                    cpporm_sqldriver::SqlValue defVal_sv = query.value(rec_meta.indexOf("Default"));
                    colInfo.default_value = defVal_sv.isNull() ? "" : defVal_sv.toString();
                    colInfo.collation_name = query.value(rec_meta.indexOf("Collation")).toString();
                    colInfo.column_key = query.value(rec_meta.indexOf("Key")).toString();
                    colInfo.extra = query.value(rec_meta.indexOf("Extra")).toString();
                } else if (driverNameUpper == "QPSQL") {
                    colInfo.name = query.value(rec_meta.indexOf("column_name")).toString();
                    std::string pg_udt_name = query.value(rec_meta.indexOf("udt_name")).toString();
                    std::string pg_data_type = query.value(rec_meta.indexOf("data_type")).toString();

                    if (pg_data_type.rfind("ARRAY", 0) == 0) {  // Starts with "ARRAY"
                        colInfo.type = pg_udt_name;
                        if (!colInfo.type.empty() && colInfo.type[0] == '_') colInfo.type.erase(0, 1);  // Remove leading underscore for array types
                        colInfo.type += "[]";
                    } else if (pg_udt_name.empty() || pg_udt_name == "anyelement" || pg_udt_name == "anyarray") {
                        colInfo.type = pg_data_type;
                    } else {
                        colInfo.type = pg_udt_name;
                    }
                    std::string nullable_str_pg = query.value(rec_meta.indexOf("is_nullable")).toString();
                    std::transform(nullable_str_pg.begin(), nullable_str_pg.end(), nullable_str_pg.begin(), ::toupper);
                    colInfo.is_nullable = (nullable_str_pg == "YES");
                    colInfo.default_value = query.value(rec_meta.indexOf("column_default")).toString();
                    colInfo.collation_name = query.value(rec_meta.indexOf("collation_name")).toString();
                }
                if (!colInfo.name.empty()) {
                    colInfo.normalized_type = normalizeDbType(colInfo.type, driverNameUpper);
                    columns[colInfo.name] = colInfo;
                }
            }
            return columns;
        }

        Error migrateModifyColumns(Session &session, const ModelMeta &meta, const QString &driverNameUpper) {
            qInfo() << "migrateModifyColumns: Checking columns for table '" << QString::fromStdString(meta.table_name) << "'...";
            std::map<std::string, DbColumnInfo> existing_db_columns = getTableColumnsInfo(session, QString::fromStdString(meta.table_name), driverNameUpper);

            for (const auto &model_field : meta.fields) {
                if (has_flag(model_field.flags, FieldFlag::Association) || model_field.db_name.empty()) {
                    continue;
                }

                std::string model_sql_type_str = Session::getSqlTypeForCppType(model_field, driverNameUpper);
                std::string model_normalized_sql_type = normalizeDbType(model_sql_type_str, driverNameUpper);

                auto it_db_col = existing_db_columns.find(model_field.db_name);
                if (it_db_col == existing_db_columns.end()) {  // Column doesn't exist in DB, ADD it
                    qInfo() << "migrateModifyColumns: Column '" << QString::fromStdString(model_field.db_name) << "' not found in table '" << QString::fromStdString(meta.table_name) << "'. Attempting to ADD.";

                    std::string add_col_sql_str = "ALTER TABLE " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " ADD COLUMN " + QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " " + model_sql_type_str;

                    if (has_flag(model_field.flags, FieldFlag::NotNull)) add_col_sql_str += " NOT NULL";
                    // UNIQUE constraint might be part of column def or separate ALTER TABLE ADD CONSTRAINT
                    if (has_flag(model_field.flags, FieldFlag::Unique) && !has_flag(model_field.flags, FieldFlag::PrimaryKey)) {
                        // Simpler to add as column constraint if DB supports. Some DBs (like older MySQL for some types) might need table constraint.
                        add_col_sql_str += " UNIQUE";
                    }
                    // Default value handling would go here if specified in FieldMeta and supported by ALTER ADD
                    add_col_sql_str += ";";

                    qInfo() << "migrateModifyColumns (ADD DDL): " << QString::fromStdString(add_col_sql_str);
                    auto [_, add_err] = execute_ddl_query(session.getDbHandle(), add_col_sql_str);
                    if (add_err) {
                        qWarning() << "migrateModifyColumns: Failed to ADD column '" << QString::fromStdString(model_field.db_name) << "': " << QString::fromStdString(add_err.toString());
                        // Potentially return add_err or collect errors
                    }

                } else {  // Column exists, check if it needs modification
                    DbColumnInfo &db_col = it_db_col->second;
                    bool needs_alter_type = false;
                    bool needs_alter_notnull = false;
                    // bool needs_alter_default = false; // TODO: Add default value comparison
                    // bool needs_alter_unique = false; // TODO: Add unique constraint comparison (complex as it might be separate constraint)

                    // Type comparison
                    if (model_normalized_sql_type != db_col.normalized_type) {
                        needs_alter_type = true;
                        // Avoid altering for some common compatible SQLite types where "TEXT" can store anything or "VARCHAR" is "TEXT"
                        if (driverNameUpper == "QSQLITE" && ((model_normalized_sql_type == "text" && db_col.normalized_type == "varchar") || (model_normalized_sql_type == "varchar" && db_col.normalized_type == "text"))) {
                            needs_alter_type = false;
                        }
                        // Avoid altering if model requests a narrower integer type than DB (potential data loss)
                        else if ((model_normalized_sql_type == "int" && db_col.normalized_type == "bigint") || (model_normalized_sql_type == "smallint" && (db_col.normalized_type == "int" || db_col.normalized_type == "bigint"))) {
                            qInfo() << "migrateModifyColumns: Model requests narrowing integer conversion for column '" << QString::fromStdString(model_field.db_name) << "' from DB type '" << QString::fromStdString(db_col.type) << "' to model type '" << QString::fromStdString(model_sql_type_str)
                                    << "'. Skipping automatic type alteration to prevent data loss.";
                            needs_alter_type = false;
                        }
                    }

                    // Nullability comparison
                    bool model_is_not_null = has_flag(model_field.flags, FieldFlag::NotNull);
                    if (model_is_not_null == db_col.is_nullable) {  // model_is_not_null vs db_col.is_nullable (true if can be null)
                        needs_alter_notnull = true;
                    }

                    if (needs_alter_type || needs_alter_notnull /* || needs_alter_default || needs_alter_unique */) {
                        qInfo() << "migrateModifyColumns: Mismatch or desired change for column '" << QString::fromStdString(model_field.db_name) << "'. DB type: '" << QString::fromStdString(db_col.type) << "' (norm: " << QString::fromStdString(db_col.normalized_type)
                                << "), is_nullable:" << db_col.is_nullable << ". Model type: '" << QString::fromStdString(model_sql_type_str) << "' (norm: " << QString::fromStdString(model_normalized_sql_type) << "), not_null:" << model_is_not_null << ". Attempting to MODIFY.";

                        std::string alter_col_sql_str;
                        if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                            alter_col_sql_str = "ALTER TABLE " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " MODIFY COLUMN " + QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " " + model_sql_type_str;
                            if (model_is_not_null)
                                alter_col_sql_str += " NOT NULL";
                            else
                                alter_col_sql_str += " NULL";  // Or DEFAULT NULL depending on DB
                        } else if (driverNameUpper == "QPSQL") {
                            // PostgreSQL often requires separate ALTER statements for type and nullability
                            if (needs_alter_type) {
                                std::string alter_type_sql = "ALTER TABLE " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " ALTER COLUMN " + QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " TYPE " + model_sql_type_str + ";";
                                qInfo() << "migrateModifyColumns (PG TYPE DDL): " << QString::fromStdString(alter_type_sql);
                                auto [_, alter_type_err] = execute_ddl_query(session.getDbHandle(), alter_type_sql);
                                if (alter_type_err) qWarning() << "migrateModifyColumns: Failed to MODIFY PG column TYPE for '" << QString::fromStdString(model_field.db_name) << "': " << QString::fromStdString(alter_type_err.toString());
                            }
                            if (needs_alter_notnull) {
                                std::string alter_null_sql = "ALTER TABLE " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " ALTER COLUMN " + QueryBuilder::quoteSqlIdentifier(model_field.db_name) + (model_is_not_null ? " SET NOT NULL;" : " DROP NOT NULL;");
                                qInfo() << "migrateModifyColumns (PG NULL DDL): " << QString::fromStdString(alter_null_sql);
                                auto [_, alter_null_err] = execute_ddl_query(session.getDbHandle(), alter_null_sql);
                                if (alter_null_err) qWarning() << "migrateModifyColumns: Failed to MODIFY PG column NULLABILITY for '" << QString::fromStdString(model_field.db_name) << "': " << QString::fromStdString(alter_null_err.toString());
                            }
                            // Skip combined alter_col_sql_str for PG as it's handled separately
                            continue;
                        } else if (driverNameUpper == "QSQLITE") {
                            qWarning() << "migrateModifyColumns: SQLite has very limited ALTER TABLE support for modifying columns. Change for '" << QString::fromStdString(model_field.db_name) << "' skipped.";
                            continue;  // Skip to next field
                        } else {
                            qWarning() << "migrateModifyColumns: Don't know how to alter column for driver " << driverNameUpper << ". Column '" << QString::fromStdString(model_field.db_name) << "' alteration skipped.";
                            continue;  // Skip to next field
                        }
                        alter_col_sql_str += ";";

                        qInfo() << "migrateModifyColumns (MODIFY DDL): " << QString::fromStdString(alter_col_sql_str);
                        auto [_, alter_err] = execute_ddl_query(session.getDbHandle(), alter_col_sql_str);
                        if (alter_err) {
                            qWarning() << "migrateModifyColumns: Failed to MODIFY column '" << QString::fromStdString(model_field.db_name) << "': " << QString::fromStdString(alter_err.toString());
                        }
                    }
                }
            }
            return make_ok();
        }

        std::string normalizeDbType(const std::string &db_type_raw, const QString &driverNameUpperQ) {
            std::string lower_type = db_type_raw;
            std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), [](unsigned char c) {
                return std::tolower(c);
            });
            std::string driverNameUpper = driverNameUpperQ.toStdString();

            if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                if (lower_type.rfind("int", 0) == 0 && lower_type.find("unsigned") == std::string::npos && lower_type != "tinyint(1)") return "int";
                if (lower_type.rfind("int unsigned", 0) == 0) return "int unsigned";  // More specific
                if (lower_type.rfind("bigint", 0) == 0 && lower_type.find("unsigned") == std::string::npos) return "bigint";
                if (lower_type.rfind("bigint unsigned", 0) == 0) return "bigint unsigned";  // More specific
                if (lower_type == "tinyint(1)") return "boolean";                           // Often used for bool
                if (lower_type.rfind("varchar", 0) == 0) return "varchar";
                if (lower_type.rfind("char", 0) == 0 && lower_type.find("varchar") == std::string::npos) return "char";
                if (lower_type == "text" || lower_type == "tinytext" || lower_type == "mediumtext" || lower_type == "longtext") return "text";
                if (lower_type == "datetime") return "datetime";
                if (lower_type == "timestamp") return "timestamp";
                if (lower_type == "date") return "date";
                if (lower_type == "time") return "time";
                if (lower_type == "float") return "float";
                if (lower_type == "double" || lower_type == "real") return "double";
                if (lower_type.rfind("decimal", 0) == 0 || lower_type.rfind("numeric", 0) == 0) return "decimal";
                if (lower_type == "blob" || lower_type == "tinyblob" || lower_type == "mediumblob" || lower_type == "longblob" || lower_type == "varbinary" || lower_type == "binary") return "blob";
                if (lower_type == "json") return "json";
                if (lower_type == "point" || lower_type == "geometry" /* etc for spatial types */) return "geometry";

            } else if (driverNameUpper == "QPSQL") {
                if (lower_type == "integer" || lower_type == "int4") return "int";
                if (lower_type == "bigint" || lower_type == "int8") return "bigint";
                if (lower_type == "smallint" || lower_type == "int2") return "smallint";
                if (lower_type == "boolean" || lower_type == "bool") return "boolean";
                if (lower_type.rfind("character varying", 0) == 0) return "varchar";
                if ((lower_type.rfind("character(", 0) == 0 || lower_type.rfind("char(", 0) == 0) && lower_type.find("varying") == std::string::npos) return "char";
                if (lower_type == "text") return "text";
                if (lower_type == "timestamp without time zone" || lower_type == "timestamp") return "timestamp";
                if (lower_type == "timestamp with time zone") return "timestamptz";
                if (lower_type == "date") return "date";
                if (lower_type == "time without time zone" || lower_type == "time") return "time";
                if (lower_type == "time with time zone") return "timetz";
                if (lower_type == "real" || lower_type == "float4") return "float";
                if (lower_type == "double precision" || lower_type == "float8") return "double";
                if (lower_type == "numeric" || lower_type == "decimal") return "decimal";
                if (lower_type == "bytea") return "blob";
                if (lower_type == "json" || lower_type == "jsonb") return "json";
                if (lower_type == "uuid") return "uuid";
                if (lower_type.find("[]") != std::string::npos) return "array";  // Generic array type

            } else if (driverNameUpper == "QSQLITE") {
                // SQLite types are more about affinity
                if (lower_type.find("int") != std::string::npos) return "int";                                                                              // INTEGER affinity
                if (lower_type == "text" || lower_type.find("char") != std::string::npos || lower_type.find("clob") != std::string::npos) return "text";    // TEXT affinity
                if (lower_type == "blob" || lower_type.empty() /* typeless */) return "blob";                                                               // BLOB affinity
                if (lower_type == "real" || lower_type.find("floa") != std::string::npos || lower_type.find("doub") != std::string::npos) return "double";  // REAL affinity
                // NUMERIC affinity can store various types, often used for bool, date, decimal.
                if (lower_type == "numeric" || lower_type.find("deci") != std::string::npos || lower_type.find("bool") != std::string::npos || lower_type.find("date") != std::string::npos || lower_type.find("datetime") != std::string::npos) return "numeric";
            }
            return lower_type;  // Return lowercased original if not specifically mapped
        }

    }  // namespace internal
}  // namespace cpporm