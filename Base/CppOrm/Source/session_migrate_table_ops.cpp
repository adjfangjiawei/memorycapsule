// Base/CppOrm/Source/session_migrate_table_ops.cpp
#include <QDebug>
#include <QString>  // For QString::fromStdString
#include <mutex>

#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_migrate_priv.h"
#include "cpporm_sqldriver/sql_database.h"
#include "cpporm_sqldriver/sql_error.h"
#include "cpporm_sqldriver/sql_query.h"

namespace cpporm {
    namespace internal {

        Error migrateCreateTable(Session &session, const ModelMeta &meta, const QString &driverNameUpper) {
            if (meta.table_name.empty()) {
                return Error(ErrorCode::InvalidConfiguration, "migrateCreateTable: ModelMeta has no table name.");
            }

            std::vector<std::string> column_definitions_sql;
            std::vector<std::string> pk_col_db_names_for_constraint;
            std::vector<std::string> table_constraints_sql;

            for (const auto &field : meta.fields) {
                if (has_flag(field.flags, FieldFlag::Association) || field.db_name.empty()) {
                    continue;
                }

                std::string col_def_str = QueryBuilder::quoteSqlIdentifier(field.db_name);
                col_def_str += " " + Session::getSqlTypeForCppType(field, driverNameUpper);

                if (has_flag(field.flags, FieldFlag::PrimaryKey)) {
                    pk_col_db_names_for_constraint.push_back(field.db_name);
                    if ((driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") && has_flag(field.flags, FieldFlag::AutoIncrement)) {
                        col_def_str += " AUTO_INCREMENT";
                    } else if (driverNameUpper == "QSQLITE" && pk_col_db_names_for_constraint.size() == 1 && has_flag(field.flags, FieldFlag::AutoIncrement) && (field.cpp_type == typeid(int) || field.cpp_type == typeid(long long))) {
                        // SQLite autoincrement is typically part of PRIMARY KEY for INTEGER type
                        // col_def_str += " PRIMARY KEY AUTOINCREMENT"; // This is handled by table constraint, but AUTOINCREMENT keyword is special
                        // For SQLite, if single integer PK, it's implicitly auto-incrementing (rowid alias).
                        // Explicit AUTOINCREMENT keyword has specific behavior (guarantees new ID > max existing).
                        // We'll let the PRIMARY KEY constraint handle the PK part.
                        // The getSqlTypeForCppType might return "INTEGER PRIMARY KEY AUTOINCREMENT" for SQLite if desired.
                        // For now, keeping AUTO_INCREMENT separate for MySQL and relying on PK constraint for SQLite.
                    }
                }
                if (has_flag(field.flags, FieldFlag::NotNull)) {
                    col_def_str += " NOT NULL";
                }
                if (has_flag(field.flags, FieldFlag::Unique) && !has_flag(field.flags, FieldFlag::PrimaryKey)) {
                    // Some DBs define UNIQUE as column constraint, others as table constraint.
                    // For simplicity, adding as column constraint here if not PK.
                    col_def_str += " UNIQUE";
                }
                column_definitions_sql.push_back(col_def_str);
            }

            if (!pk_col_db_names_for_constraint.empty()) {
                std::string pk_constraint_str = "PRIMARY KEY (";
                for (size_t i = 0; i < pk_col_db_names_for_constraint.size(); ++i) {
                    pk_constraint_str += QueryBuilder::quoteSqlIdentifier(pk_col_db_names_for_constraint[i]);
                    if (i < pk_col_db_names_for_constraint.size() - 1) pk_constraint_str += ", ";
                }
                pk_constraint_str += ")";
                // For SQLite, if single integer PK and AUTOINCREMENT desired by model flag
                if (driverNameUpper == "QSQLITE" && pk_col_db_names_for_constraint.size() == 1) {
                    const FieldMeta *pk_field = meta.findFieldByDbName(pk_col_db_names_for_constraint[0]);
                    if (pk_field && has_flag(pk_field->flags, FieldFlag::AutoIncrement) && (pk_field->cpp_type == typeid(int) || pk_field->cpp_type == typeid(long long))) {
                        // Session::getSqlTypeForCppType should return "INTEGER" for this.
                        // The PK constraint with AUTOINCREMENT for SQLite is on the column def.
                        // This logic is a bit tricky here vs column def.
                        // Let's assume column def for INTEGER PK implicitly handles rowid.
                        // For explicit AUTOINCREMENT keyword, it's more specific.
                        // For now, standard PK constraint.
                    }
                }
                table_constraints_sql.push_back(pk_constraint_str);
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
                        qWarning() << "migrateCreateTable (FK): Could not determine target "
                                      "table/PK for BelongsTo association '"
                                   << QString::fromStdString(assoc.cpp_field_name) << "' on table '" << QString::fromStdString(meta.table_name) << "'. FK constraint not created.";
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

            if (!all_definitions_sql_str.empty()) {
                bool ends_with_comma_space = true;
                while (ends_with_comma_space && !all_definitions_sql_str.empty()) {
                    char last_char = all_definitions_sql_str.back();
                    if (last_char == ',' || last_char == ' ') {
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

            std::string create_table_ddl_std = "CREATE TABLE IF NOT EXISTS " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " (" + all_definitions_sql_str + ");";

            qInfo() << "migrateCreateTable (DDL for " << QString::fromStdString(meta.table_name) << "): " << QString::fromStdString(create_table_ddl_std);

            auto [_, err_obj] = execute_ddl_query(session.getDbHandle(), create_table_ddl_std);
            return err_obj;
        }

        std::pair<cpporm_sqldriver::SqlQuery, Error> execute_ddl_query(cpporm_sqldriver::SqlDatabase &db, const std::string &ddl_sql_std) {
            if (!db.isOpen()) {
                if (!db.open()) {
                    cpporm_sqldriver::SqlError err = db.lastError();
                    qWarning() << "execute_ddl_query: Failed to open database for DDL:" << QString::fromStdString(err.text()) << "SQL:" << QString::fromStdString(ddl_sql_std);
                    // Explicitly construct the pair
                    return std::make_pair(cpporm_sqldriver::SqlQuery(db), Error(ErrorCode::ConnectionNotOpen, "Failed to open database for DDL: " + err.text(), err.nativeErrorCodeNumeric()));
                }
            }
            // Session::execute_query_internal is public static now
            return Session::execute_query_internal(db, ddl_sql_std, {});
        }

    }  // namespace internal
}  // namespace cpporm