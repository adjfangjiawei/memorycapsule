// Base/CppOrm/Source/session_migrate_table_ops.cpp
#include <QDebug>
#include <QString>
#include <mutex>  // For model factory registry lock

#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_migrate_priv.h"  // Contains declarations
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_query.h"

namespace cpporm {
    namespace internal {

        Error migrateCreateTable(Session &session, const ModelMeta &meta, const QString &driverNameUpper) {
            if (meta.table_name.empty()) {
                return Error(ErrorCode::InvalidConfiguration, "migrateCreateTable: ModelMeta has no table name.");
            }

            std::vector<std::string> column_definitions_sql;
            std::vector<std::string> pk_col_db_names_for_table_constraint;  // Renamed for clarity
            std::vector<std::string> table_constraints_sql;

            for (const auto &field : meta.fields) {  // Loop variable is 'field'
                if (has_flag(field.flags, FieldFlag::Association) || field.db_name.empty()) {
                    continue;
                }

                std::string col_def_str = QueryBuilder::quoteSqlIdentifier(field.db_name);
                std::string field_sql_type = Session::getSqlTypeForCppType(field, driverNameUpper);

                // Special handling for SQLite INTEGER PRIMARY KEY AUTOINCREMENT
                if (driverNameUpper == "QSQLITE" && has_flag(field.flags, FieldFlag::PrimaryKey) && has_flag(field.flags, FieldFlag::AutoIncrement) && (field.cpp_type == typeid(int) || field.cpp_type == typeid(long long))) {
                    // For SQLite, "INTEGER PRIMARY KEY AUTOINCREMENT" is a column definition.
                    // Ensure getSqlTypeForCppType returns "INTEGER" and we add the rest.
                    if (field_sql_type == "INTEGER") {  // Assuming getSqlType returns base type
                        field_sql_type += " PRIMARY KEY AUTOINCREMENT";
                    } else {
                        qWarning() << "migrateCreateTable: SQLite AUTOINCREMENT PK '" << QString::fromStdString(field.db_name) << "' is not INTEGER type. AUTOINCREMENT keyword might not apply as expected.";
                    }
                    // This column now defines its own PK, so don't add to table-level PK constraint later
                    // if it's the only PK. If composite, table constraint is still needed but this col is already PK.
                } else {
                    if (has_flag(field.flags, FieldFlag::PrimaryKey)) {
                        pk_col_db_names_for_table_constraint.push_back(field.db_name);
                    }
                }

                col_def_str += " " + field_sql_type;

                if ((driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") && has_flag(field.flags, FieldFlag::AutoIncrement) && field_sql_type.find("AUTO_INCREMENT") == std::string::npos &&  // Check if already added by type
                    col_def_str.find("AUTO_INCREMENT") == std::string::npos) {  // Check if already added to col_def_str
                    col_def_str += " AUTO_INCREMENT";
                }

                // NOT NULL constraint (unless already part of PRIMARY KEY for some DBs, but usually separate)
                if (has_flag(field.flags, FieldFlag::NotNull)) {
                    // Avoid redundant "NOT NULL" if type string implies it (e.g., from "INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL")
                    if (field_sql_type.find("NOT NULL") == std::string::npos && col_def_str.find("NOT NULL") == std::string::npos) {
                        col_def_str += " NOT NULL";
                    }
                }

                // UNIQUE constraint for non-primary key columns (handled by column def or explicit index)
                if (has_flag(field.flags, FieldFlag::Unique) && !has_flag(field.flags, FieldFlag::PrimaryKey)) {
                    bool part_of_model_unique_index = false;
                    for (const auto &idx_def : meta.indexes) {
                        if (idx_def.is_unique && idx_def.db_column_names.size() == 1 && idx_def.db_column_names[0] == field.db_name) {
                            part_of_model_unique_index = true;
                            break;
                        }
                    }
                    if (!part_of_model_unique_index && col_def_str.find("UNIQUE") == std::string::npos) {
                        col_def_str += " UNIQUE";
                    }
                }
                column_definitions_sql.push_back(col_def_str);
            }

            // Add table-level PRIMARY KEY constraint if not handled by column def (e.g., composite PKs, or non-SQLite single PKs)
            if (!pk_col_db_names_for_table_constraint.empty()) {
                bool sqlite_single_int_pk_handled_by_col = false;
                if (driverNameUpper == "QSQLITE" && pk_col_db_names_for_table_constraint.size() == 1) {
                    const FieldMeta *pk_field = meta.findFieldByDbName(pk_col_db_names_for_table_constraint[0]);
                    if (pk_field && has_flag(pk_field->flags, FieldFlag::AutoIncrement) && (Session::getSqlTypeForCppType(*pk_field, driverNameUpper).find("PRIMARY KEY AUTOINCREMENT") != std::string::npos)) {
                        sqlite_single_int_pk_handled_by_col = true;
                    } else if (pk_field && (Session::getSqlTypeForCppType(*pk_field, driverNameUpper).find("PRIMARY KEY") != std::string::npos)) {
                        // If type already included "PRIMARY KEY" (e.g. from modified getSqlTypeForCppType for SQLite)
                        sqlite_single_int_pk_handled_by_col = true;
                    }
                }

                if (!sqlite_single_int_pk_handled_by_col) {
                    std::string pk_constraint_str = "PRIMARY KEY (";
                    for (size_t i = 0; i < pk_col_db_names_for_table_constraint.size(); ++i) {
                        pk_constraint_str += QueryBuilder::quoteSqlIdentifier(pk_col_db_names_for_table_constraint[i]);
                        if (i < pk_col_db_names_for_table_constraint.size() - 1) pk_constraint_str += ", ";
                    }
                    pk_constraint_str += ")";
                    table_constraints_sql.push_back(pk_constraint_str);
                }
            }

            for (const auto &assoc : meta.associations) {
                if (assoc.type == AssociationType::BelongsTo && !assoc.foreign_key_db_name.empty()) {
                    std::string fk_col_on_curr_table = assoc.foreign_key_db_name;
                    std::string target_table_name_str;
                    std::string target_pk_col_name_str;

                    cpporm::internal::ModelFactory factory_fn;
                    {
                        std::lock_guard<std::mutex> lock(cpporm::internal::getGlobalModelFactoryRegistryMutex());
                        auto it_factory = cpporm::internal::getGlobalModelFactoryRegistry().find(assoc.target_model_type);
                        if (it_factory != cpporm::internal::getGlobalModelFactoryRegistry().end()) {
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
                        std::string fk_sql = "FOREIGN KEY (" + QueryBuilder::quoteSqlIdentifier(fk_col_on_curr_table) + ")" + " REFERENCES " + QueryBuilder::quoteSqlIdentifier(target_table_name_str) + " (" + QueryBuilder::quoteSqlIdentifier(target_pk_col_name_str) + ")";
                        table_constraints_sql.push_back(fk_sql);
                    } else {
                        qWarning() << "migrateCreateTable (FK): Could not determine target table/PK for BelongsTo association '" << QString::fromStdString(assoc.cpp_field_name) << "' on table '" << QString::fromStdString(meta.table_name) << "'. FK constraint not created.";
                    }
                }
            }

            std::string all_definitions_sql_str;
            for (size_t i = 0; i < column_definitions_sql.size(); ++i) {
                all_definitions_sql_str += column_definitions_sql[i];
                if (i < column_definitions_sql.size() - 1 || !table_constraints_sql.empty()) {
                    all_definitions_sql_str += ", ";
                }
            }
            for (size_t i = 0; i < table_constraints_sql.size(); ++i) {
                all_definitions_sql_str += table_constraints_sql[i];
                if (i < table_constraints_sql.size() - 1) {
                    all_definitions_sql_str += ", ";
                }
            }
            while (!all_definitions_sql_str.empty() && (all_definitions_sql_str.back() == ',' || all_definitions_sql_str.back() == ' ')) {
                all_definitions_sql_str.pop_back();
            }

            if (all_definitions_sql_str.empty()) {
                return Error(ErrorCode::InvalidConfiguration, "migrateCreateTable: No column definitions or constraints generated for table '" + meta.table_name + "'.");
            }

            std::string create_table_ddl_std = "CREATE TABLE IF NOT EXISTS " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " (" + all_definitions_sql_str + ");";

            qInfo() << "migrateCreateTable (DDL for " << QString::fromStdString(meta.table_name) << "): " << QString::fromStdString(create_table_ddl_std);

            auto [_, err_obj] = execute_ddl_query(session.getDbHandle(), create_table_ddl_std);
            return err_obj;
        }

        // execute_ddl_query is correctly defined here or in session_migrate_priv.h/utils
        // ... (execute_ddl_query definition as before) ...
        std::pair<cpporm_sqldriver::SqlQuery, Error> execute_ddl_query(cpporm_sqldriver::SqlDatabase &db, const std::string &ddl_sql_std) {
            if (!db.isOpen()) {
                if (!db.open()) {
                    cpporm_sqldriver::SqlError err = db.lastError();
                    qWarning() << "execute_ddl_query: Failed to open database for DDL:" << QString::fromStdString(err.text()) << "SQL:" << QString::fromStdString(ddl_sql_std);
                    return std::make_pair(cpporm_sqldriver::SqlQuery(db), Error(ErrorCode::ConnectionNotOpen, "Failed to open database for DDL: " + err.text(), err.nativeErrorCodeNumeric()));
                }
            }
            return Session::execute_query_internal(db, ddl_sql_std, {});
        }

    }  // namespace internal
}  // namespace cpporm