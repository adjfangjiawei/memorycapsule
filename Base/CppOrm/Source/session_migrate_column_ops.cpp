// Base/CppOrm/Source/session_migrate_column_ops.cpp
#include <QDebug>
#include <QString>
#include <algorithm>
#include <cctype>

#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_migrate_priv.h"
#include "sqldriver/i_sql_driver.h"  // ***** 新增: 包含 ISqlDriver 接口 *****
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_field.h"
#include "sqldriver/sql_query.h"
#include "sqldriver/sql_record.h"
#include "sqldriver/sql_value.h"

namespace cpporm {
    namespace internal {
        // ... (getTableColumnsInfo a在这里保持不变，因为它已经在上次的提交中被正确修改以填充 comment) ...
        // ... getTableColumnsInfo implementation remains the same as in previous correct submission ...
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
                        qWarning() << "getTableColumnsInfo (SQLite): Failed to query PRAGMA table_info/table_xinfo for table" << tableNameQString << ":" << QString::fromStdString(query.lastError().text());
                        return columns;
                    }
                }
            } else if (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                sql_std = "SHOW FULL COLUMNS FROM " + QueryBuilder::quoteSqlIdentifier(tableNameStd) + ";";
                if (!query.exec(sql_std)) {
                    qWarning() << "getTableColumnsInfo (MySQL/MariaDB): Failed to query SHOW FULL COLUMNS for table" << tableNameQString << ":" << QString::fromStdString(query.lastError().text()) << "SQL:" << QString::fromStdString(sql_std);
                    return columns;
                }
            } else if (driverNameUpper == "QPSQL" || driverNameUpper == "POSTGRESQL") {
                sql_std =
                    "SELECT c.column_name, c.data_type, c.udt_name, c.is_nullable, "
                    "c.column_default, "
                    "c.character_maximum_length, c.numeric_precision, c.numeric_scale, "
                    "c.collation_name, pgd.description AS column_comment "
                    "FROM information_schema.columns c "
                    "LEFT JOIN pg_catalog.pg_statio_all_tables AS st ON (st.relname = c.table_name) "
                    "LEFT JOIN pg_catalog.pg_description pgd ON (pgd.objoid = st.relid AND pgd.objsubid = c.ordinal_position) "
                    "WHERE c.table_schema = current_schema() AND c.table_name = '" +
                    tableNameStd + "';";
                if (!query.exec(sql_std)) {
                    qWarning() << "getTableColumnsInfo (PostgreSQL): Failed to query information_schema.columns for table" << tableNameQString << ":" << QString::fromStdString(query.lastError().text()) << "SQL:" << QString::fromStdString(sql_std);
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
                } else if (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                    colInfo.name = query.value(rec_meta.indexOf("Field")).toString();
                    cpporm_sqldriver::SqlValue type_val = query.value(rec_meta.indexOf("Type"));
                    if (type_val.type() == cpporm_sqldriver::SqlValueType::ByteArray) {
                        std::vector<unsigned char> bytes = type_val.toStdVectorUChar();
                        colInfo.type = std::string(bytes.begin(), bytes.end());
                    } else {
                        colInfo.type = type_val.toString();
                    }
                    std::string nullable_str = query.value(rec_meta.indexOf("Null")).toString();
                    std::transform(nullable_str.begin(), nullable_str.end(), nullable_str.begin(), ::toupper);
                    colInfo.is_nullable = (nullable_str == "YES");
                    cpporm_sqldriver::SqlValue defVal_sv = query.value(rec_meta.indexOf("Default"));
                    colInfo.default_value = defVal_sv.isNull() ? "" : defVal_sv.toString();
                    colInfo.collation_name = query.value(rec_meta.indexOf("Collation")).toString();
                    colInfo.column_key = query.value(rec_meta.indexOf("Key")).toString();
                    colInfo.extra = query.value(rec_meta.indexOf("Extra")).toString();
                    colInfo.comment = query.value(rec_meta.indexOf("Comment")).toString();
                } else if (driverNameUpper == "QPSQL" || driverNameUpper == "POSTGRESQL") {
                    colInfo.name = query.value(rec_meta.indexOf("column_name")).toString();
                    std::string pg_udt_name = query.value(rec_meta.indexOf("udt_name")).toString();
                    std::string pg_data_type = query.value(rec_meta.indexOf("data_type")).toString();

                    if (pg_data_type.rfind("ARRAY", 0) == 0) {
                        colInfo.type = pg_udt_name;
                        if (!colInfo.type.empty() && colInfo.type[0] == '_') colInfo.type.erase(0, 1);
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
                    colInfo.comment = query.value(rec_meta.indexOf("column_comment")).toString();
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
                if (it_db_col == existing_db_columns.end()) {
                    // ... (ADD COLUMN logic remains the same as in previous submission, with corrected escapeString call)
                    qInfo() << "migrateModifyColumns: Column '" << QString::fromStdString(model_field.db_name) << "' not found in existing DB schema for table '" << QString::fromStdString(meta.table_name) << "'. Attempting to ADD.";
                    std::string add_col_sql_str = "ALTER TABLE " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " ADD COLUMN " + QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " " + model_sql_type_str;
                    if (has_flag(model_field.flags, FieldFlag::NotNull)) add_col_sql_str += " NOT NULL";
                    if (has_flag(model_field.flags, FieldFlag::Unique) && !has_flag(model_field.flags, FieldFlag::PrimaryKey)) add_col_sql_str += " UNIQUE";
                    if ((driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") && has_flag(model_field.flags, FieldFlag::AutoIncrement) && model_sql_type_str.find("AUTO_INCREMENT") == std::string::npos)
                        add_col_sql_str += " AUTO_INCREMENT";
                    if (!model_field.comment.empty() && (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB")) {
                        if (session.getDbHandle().driver()) add_col_sql_str += " COMMENT '" + session.getDbHandle().driver()->escapeString(model_field.comment) + "'";
                    }
                    add_col_sql_str += ";";
                    qInfo() << "migrateModifyColumns (ADD DDL): " << QString::fromStdString(add_col_sql_str);
                    auto [_, add_err] = execute_ddl_query(session.getDbHandle(), add_col_sql_str);
                    if (add_err)
                        qWarning() << "migrateModifyColumns: Failed to ADD column '" << QString::fromStdString(model_field.db_name) << "': " << QString::fromStdString(add_err.toString());
                    else if (!model_field.comment.empty() && (driverNameUpper == "QPSQL" || driverNameUpper == "POSTGRESQL")) {
                        if (session.getDbHandle().driver()) {
                            std::string pg_comment_sql = "COMMENT ON COLUMN " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + "." + QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " IS '" + session.getDbHandle().driver()->escapeString(model_field.comment) + "';";
                            qInfo() << "migrateModifyColumns (PG COMMENT DDL): " << QString::fromStdString(pg_comment_sql);
                            execute_ddl_query(session.getDbHandle(), pg_comment_sql);
                        }
                    }
                } else {
                    DbColumnInfo &db_col = it_db_col->second;
                    bool needs_alter_type = false;
                    bool needs_alter_notnull = false;
                    bool needs_alter_comment = false;

                    if (model_normalized_sql_type != db_col.normalized_type) {
                        needs_alter_type = true;
                        if (driverNameUpper == "QSQLITE" && ((model_normalized_sql_type == "text" && db_col.normalized_type == "varchar") || (model_normalized_sql_type == "varchar" && db_col.normalized_type == "text"))) {
                            needs_alter_type = false;
                        } else if ((model_normalized_sql_type == "int" && db_col.normalized_type == "bigint") || (model_normalized_sql_type == "smallint" && (db_col.normalized_type == "int" || db_col.normalized_type == "bigint"))) {
                            qInfo() << "migrateModifyColumns: Model requests narrowing integer conversion for column '" << QString::fromStdString(model_field.db_name) << "' from DB type '" << QString::fromStdString(db_col.type) << "' to model type '" << QString::fromStdString(model_sql_type_str)
                                    << "'. Skipping automatic type alteration to prevent data loss.";
                            needs_alter_type = false;
                        }
                    }

                    bool model_is_not_null = has_flag(model_field.flags, FieldFlag::NotNull);
                    if (model_is_not_null == db_col.is_nullable) {
                        needs_alter_notnull = true;
                    }

                    if (model_field.comment != db_col.comment) {
                        needs_alter_comment = true;
                    }

                    if (needs_alter_type || needs_alter_notnull || needs_alter_comment) {
                        qInfo() << "migrateModifyColumns: Mismatch or desired change for column '" << QString::fromStdString(model_field.db_name) << "'. DB type: '" << QString::fromStdString(db_col.type) << "', nullable:" << db_col.is_nullable << ", comment: '"
                                << QString::fromStdString(db_col.comment) << "'. Model type: '" << QString::fromStdString(model_sql_type_str) << "', not_null:" << model_is_not_null << ", comment: '" << QString::fromStdString(model_field.comment) << "'. Attempting to MODIFY.";

                        if (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                            std::string alter_col_sql_str_main = "ALTER TABLE " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " MODIFY COLUMN " + QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " " + model_sql_type_str;
                            if (model_is_not_null || has_flag(model_field.flags, FieldFlag::PrimaryKey))
                                alter_col_sql_str_main += " NOT NULL";
                            else
                                alter_col_sql_str_main += " NULL";
                            if (has_flag(model_field.flags, FieldFlag::AutoIncrement) && model_sql_type_str.find("AUTO_INCREMENT") == std::string::npos) alter_col_sql_str_main += " AUTO_INCREMENT";
                            if (!model_field.comment.empty()) {
                                if (session.getDbHandle().driver()) alter_col_sql_str_main += " COMMENT '" + session.getDbHandle().driver()->escapeString(model_field.comment) + "'";
                            }
                            alter_col_sql_str_main += ";";

                            qInfo() << "migrateModifyColumns (MODIFY DDL): " << QString::fromStdString(alter_col_sql_str_main);
                            auto [_, alter_err] = execute_ddl_query(session.getDbHandle(), alter_col_sql_str_main);
                            if (alter_err) qWarning() << "migrateModifyColumns: Failed to MODIFY column '" << QString::fromStdString(model_field.db_name) << "': " << QString::fromStdString(alter_err.toString());
                        } else if (driverNameUpper == "QPSQL" || driverNameUpper == "POSTGRESQL") {
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
                            if (needs_alter_comment) {
                                if (session.getDbHandle().driver()) {
                                    std::string pg_comment_sql = "COMMENT ON COLUMN " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + "." + QueryBuilder::quoteSqlIdentifier(model_field.db_name) + " IS '" + session.getDbHandle().driver()->escapeString(model_field.comment) + "';";
                                    qInfo() << "migrateModifyColumns (PG COMMENT DDL): " << QString::fromStdString(pg_comment_sql);
                                    execute_ddl_query(session.getDbHandle(), pg_comment_sql);
                                }
                            }
                        } else if (driverNameUpper == "QSQLITE") {
                            qWarning() << "migrateModifyColumns: SQLite has very limited ALTER TABLE support. Change for '" << QString::fromStdString(model_field.db_name) << "' skipped.";
                        } else {
                            qWarning() << "migrateModifyColumns: Don't know how to alter column for driver " << driverNameUpper << ". Column '" << QString::fromStdString(model_field.db_name) << "' alteration skipped.";
                        }
                    }
                }
            }
            return make_ok();
        }

    }  // namespace internal
}  // namespace cpporm