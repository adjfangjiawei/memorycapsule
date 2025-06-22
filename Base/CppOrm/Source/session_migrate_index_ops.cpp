// Base/CppOrm/Source/session_migrate_index_ops.cpp
#include <QDebug>
#include <QHash>  // For qHash
#include <QString>
#include <algorithm>
#include <set>

#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_migrate_priv.h"  // Should contain normalizeDbType declaration
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_field.h"
#include "sqldriver/sql_query.h"
#include "sqldriver/sql_record.h"

namespace cpporm {
    namespace internal {

        // 确保此函数定义存在且未被注释掉
        bool areIndexDefinitionsEquivalent(const DbIndexInfo &db_idx, const IndexDefinition &model_idx_def, const QString &driverNameUpper) {
            (void)driverNameUpper;  // Not used currently but could be for driver-specific logic
            if (db_idx.is_unique != model_idx_def.is_unique) return false;
            if (db_idx.column_names.size() != model_idx_def.db_column_names.size()) return false;

            // Column order matters for index definition equivalence
            for (size_t i = 0; i < db_idx.column_names.size(); ++i) {
                std::string db_col_lower = db_idx.column_names[i];
                std::string model_col_lower = model_idx_def.db_column_names[i];
                std::transform(db_col_lower.begin(), db_col_lower.end(), db_col_lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                std::transform(model_col_lower.begin(), model_col_lower.end(), model_col_lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (db_col_lower != model_col_lower) return false;
            }

            // Compare index type/method if both are specified (case-insensitive)
            if (!model_idx_def.type_str.empty() && !db_idx.type_method.empty()) {
                std::string model_type_lower = model_idx_def.type_str;
                std::string db_type_lower = db_idx.type_method;
                std::transform(model_type_lower.begin(), model_type_lower.end(), model_type_lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                std::transform(db_type_lower.begin(), db_type_lower.end(), db_type_lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (model_type_lower != db_type_lower) {
                    return false;
                }
            } else if (!model_idx_def.type_str.empty() && db_idx.type_method.empty()) {
                // Model specifies a type, DB does not report one (or reports default which was filtered out or is empty)
                // This could be a difference if the DB default type is not what model expects and model explicitly wants something else.
                return false;
            }
            // Note: Index condition (model_idx_def.condition_str for partial indexes) is not deeply compared here.
            // A full comparison would require parsing DB index definition or more detailed DB schema queries.
            return true;
        }

        std::map<std::string, DbIndexInfo> getTableIndexesInfo(Session &session, const QString &tableNameQString, const QString &driverNameUpper) {
            std::map<std::string, DbIndexInfo> indexes;
            cpporm_sqldriver::SqlQuery query(session.getDbHandle());
            std::string sql_std;
            std::string tableNameStd = tableNameQString.toStdString();

            if (driverNameUpper == "QSQLITE") {
                sql_std = "PRAGMA index_list(" + QueryBuilder::quoteSqlIdentifier(tableNameStd) + ");";
                if (!query.exec(sql_std)) {
                    qWarning() << "getTableIndexesInfo (SQLite): Failed to get index list for" << tableNameQString << ":" << QString::fromStdString(query.lastError().text());
                    return indexes;
                }
                std::vector<DbIndexInfo> temp_index_list_sqlite;
                cpporm_sqldriver::SqlRecord rec_meta_idxlist = query.recordMetadata();
                while (query.next()) {
                    DbIndexInfo idx_base_info;
                    idx_base_info.index_name = query.value(rec_meta_idxlist.indexOf("name")).toString();
                    idx_base_info.is_unique = query.value(rec_meta_idxlist.indexOf("unique")).toInt32() == 1;
                    std::string origin = query.value(rec_meta_idxlist.indexOf("origin")).toString();
                    if (QString::fromStdString(idx_base_info.index_name).startsWith("sqlite_autoindex_") || origin == "pk" || origin == "u") {  // SQLite 自动索引或主键/唯一约束索引
                        if (origin == "pk") idx_base_info.is_primary_key = true;                                                                // 标记是主键
                        // continue; // 不再跳过，而是收集它们，并在后面与模型定义比较时可能跳过
                    }
                    temp_index_list_sqlite.push_back(idx_base_info);
                }

                for (DbIndexInfo &idx_info_ref : temp_index_list_sqlite) {
                    std::string idx_info_sql_std = "PRAGMA index_xinfo(" + QueryBuilder::quoteSqlIdentifier(idx_info_ref.index_name) + ");";
                    if (!query.exec(idx_info_sql_std)) {
                        idx_info_sql_std = "PRAGMA index_info(" + QueryBuilder::quoteSqlIdentifier(idx_info_ref.index_name) + ");";  // Fallback for older SQLite
                        if (!query.exec(idx_info_sql_std)) {
                            qWarning() << "getTableIndexesInfo (SQLite): Failed to get info for index" << QString::fromStdString(idx_info_ref.index_name) << ":" << QString::fromStdString(query.lastError().text());
                            continue;
                        }
                    }
                    std::vector<std::pair<int, std::string>> col_order_pairs;
                    cpporm_sqldriver::SqlRecord rec_meta_idxinfo = query.recordMetadata();
                    bool use_cid = rec_meta_idxinfo.contains("cid");  // Check if 'cid' (column ID) column exists for sorting

                    while (query.next()) {
                        cpporm_sqldriver::SqlValue col_name_val = query.value(rec_meta_idxinfo.indexOf("name"));
                        std::string col_name_str = col_name_val.isNull() ? "" : col_name_val.toString();
                        if (!col_name_str.empty()) {
                            col_order_pairs.push_back({query.value(rec_meta_idxinfo.indexOf(use_cid ? "cid" : "seqno")).toInt32(), col_name_str});
                        }
                    }
                    std::sort(col_order_pairs.begin(), col_order_pairs.end());  // Sort by sequence number
                    for (const auto &p : col_order_pairs) idx_info_ref.column_names.push_back(p.second);

                    if (!idx_info_ref.column_names.empty()) indexes[idx_info_ref.index_name] = idx_info_ref;
                }

            } else if (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                sql_std = "SHOW INDEX FROM " + QueryBuilder::quoteSqlIdentifier(tableNameStd) + ";";
                if (!query.exec(sql_std)) {
                    qWarning() << "getTableIndexesInfo (MySQL/MariaDB): Failed for table" << tableNameQString << ":" << QString::fromStdString(query.lastError().text()) << "SQL:" << QString::fromStdString(sql_std);
                    return indexes;
                }

                // SHOW INDEX 返回每个索引列一行，需要聚合
                std::map<std::string, DbIndexInfo> temp_building_indexes;
                cpporm_sqldriver::SqlRecord rec_meta_mysql = query.recordMetadata();
                while (query.next()) {
                    std::string idx_name_str = query.value(rec_meta_mysql.indexOf("Key_name")).toString();
                    DbIndexInfo &current_idx_ref = temp_building_indexes[idx_name_str];  // Creates if not exists

                    if (current_idx_ref.index_name.empty()) {  // First time seeing this index name
                        current_idx_ref.index_name = idx_name_str;
                        current_idx_ref.is_unique = (query.value(rec_meta_mysql.indexOf("Non_unique")).toInt32() == 0);
                        current_idx_ref.is_primary_key = (idx_name_str == "PRIMARY");
                        current_idx_ref.type_method = query.value(rec_meta_mysql.indexOf("Index_type")).toString();
                    }

                    unsigned int seq = query.value(rec_meta_mysql.indexOf("Seq_in_index")).toUInt32();
                    std::string col_name_to_add = query.value(rec_meta_mysql.indexOf("Column_name")).toString();
                    if (current_idx_ref.column_names.size() < seq) {
                        current_idx_ref.column_names.resize(seq);
                    }
                    current_idx_ref.column_names[seq - 1] = col_name_to_add;  // Seq_in_index is 1-based
                }
                for (const auto &pair_val : temp_building_indexes) {
                    // if (pair_val.second.is_primary_key) continue; // 不再跳过主键，让areIndexDefinitionsEquivalent处理
                    if (!pair_val.second.column_names.empty()) indexes[pair_val.first] = pair_val.second;
                }

            } else if (driverNameUpper == "QPSQL" || driverNameUpper == "POSTGRESQL") {
                sql_std =
                    "SELECT idx.relname AS index_name, att.attname AS column_name, "
                    "i.indisunique AS is_unique, "
                    "i.indisprimary AS is_primary, am.amname AS index_type, "
                    "array_position(i.indkey, att.attnum) as column_seq "  // 使用 array_position 获取顺序
                    "FROM   pg_index i "
                    "JOIN   pg_class tbl ON tbl.oid = i.indrelid "
                    "JOIN   pg_class idx ON idx.oid = i.indexrelid "
                    "JOIN   pg_attribute att ON att.attrelid = tbl.oid AND att.attnum = ANY(i.indkey) "
                    "LEFT JOIN pg_am am ON am.oid = idx.relam "
                    "WHERE  tbl.relname = '" +
                    tableNameStd +
                    "' AND tbl.relnamespace = (SELECT oid FROM pg_namespace WHERE nspname = current_schema()) "
                    "ORDER BY index_name, column_seq;";  // 按索引名和列顺序排序
                if (!query.exec(sql_std)) {
                    qWarning() << "getTableIndexesInfo (PostgreSQL): Failed for table" << tableNameQString << ":" << QString::fromStdString(query.lastError().text()) << "SQL:" << QString::fromStdString(sql_std);
                    return indexes;
                }

                std::map<std::string, DbIndexInfo> temp_building_indexes_pg;
                cpporm_sqldriver::SqlRecord rec_meta_pg = query.recordMetadata();
                while (query.next()) {
                    std::string idx_name_str = query.value(rec_meta_pg.indexOf("index_name")).toString();
                    DbIndexInfo &current_idx_ref = temp_building_indexes_pg[idx_name_str];

                    if (current_idx_ref.index_name.empty()) {
                        current_idx_ref.index_name = idx_name_str;
                        current_idx_ref.is_unique = query.value(rec_meta_pg.indexOf("is_unique")).toBool();
                        current_idx_ref.is_primary_key = query.value(rec_meta_pg.indexOf("is_primary")).toBool();
                        current_idx_ref.type_method = query.value(rec_meta_pg.indexOf("index_type")).toString();
                    }
                    // 列已经按 column_seq 排序，所以直接追加
                    current_idx_ref.column_names.push_back(query.value(rec_meta_pg.indexOf("column_name")).toString());
                }
                for (const auto &pair_val : temp_building_indexes_pg) {
                    // if (pair_val.second.is_primary_key) continue; // 不再跳过
                    if (!pair_val.second.column_names.empty()) indexes[pair_val.first] = pair_val.second;
                }
            } else {
                qWarning() << "getTableIndexesInfo: Unsupported driver for index info:" << driverNameUpper;
            }
            return indexes;
        }

        Error migrateManageIndexes(Session &session, const ModelMeta &meta, const QString &driverNameUpper) {
            qInfo() << "migrateManageIndexes: Managing indexes for table '" << QString::fromStdString(meta.table_name) << "'...";
            std::map<std::string, DbIndexInfo> existing_db_indexes = getTableIndexesInfo(session, QString::fromStdString(meta.table_name), driverNameUpper);

            // Log an informational message if no indexes are found, but don't treat as error immediately.
            if (existing_db_indexes.empty() && (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB")) {
                qInfo() << "migrateManageIndexes: getTableIndexesInfo returned no user-defined indexes for table '" << QString::fromStdString(meta.table_name) << "' (MySQL/MariaDB). This is normal if only PRIMARY KEY exists or table is new.";
            }

            std::set<std::string> model_index_names_processed;

            for (const auto &model_idx_def_const : meta.indexes) {
                IndexDefinition model_idx_def = model_idx_def_const;  // Work with a copy to modify name if needed
                if (model_idx_def.db_column_names.empty()) {
                    qWarning() << "migrateManageIndexes: Model index definition for table '" << QString::fromStdString(meta.table_name) << "' (intended name: '" << QString::fromStdString(model_idx_def.index_name) << "') has no columns. Skipping.";
                    continue;
                }

                // Auto-generate index name if not provided
                if (model_idx_def.index_name.empty()) {
                    std::string auto_name_str = (model_idx_def.is_unique ? "uix_" : "idx_") + meta.table_name;
                    for (const auto &col_name_std : model_idx_def.db_column_names) {
                        std::string temp_col_name = col_name_std;
                        std::replace_if(
                            temp_col_name.begin(),
                            temp_col_name.end(),
                            [](char c) {
                                return !std::isalnum(c) && c != '_';
                            },
                            '_');
                        auto_name_str += "_" + temp_col_name;
                    }
                    // Ensure name is not too long for MySQL/MariaDB (common limit is 64 chars)
                    if (auto_name_str.length() > 60) {  // Leave some room for potential suffixes from DB
                        QString q_auto_name_str = QString::fromStdString(auto_name_str);
                        // Use qHash for a short, somewhat unique suffix
                        uint hash_val = qHash(q_auto_name_str + QString::number(model_idx_def.is_unique ? 1 : 0));
                        QString hash_suffix = QString::number(hash_val, 16).left(8);  // 8 hex chars
                        auto_name_str = q_auto_name_str.left(static_cast<int>(60 - 1 - hash_suffix.length())).toStdString() + "_" + hash_suffix.toStdString();
                    }
                    model_idx_def.index_name = auto_name_str;
                }
                model_index_names_processed.insert(model_idx_def.index_name);

                auto it_db_idx = existing_db_indexes.find(model_idx_def.index_name);
                bool needs_create = true;
                bool needs_drop_first = false;

                if (it_db_idx != existing_db_indexes.end()) {
                    if (it_db_idx->second.is_primary_key && model_idx_def.index_name == "PRIMARY") {  // MySQL PRIMARY KEY
                        qInfo() << "migrateManageIndexes: Model index definition for PRIMARY KEY on '" << QString::fromStdString(meta.table_name) << "' matches DB PRIMARY KEY. Management delegated to column/table PK definition.";
                        needs_create = false;
                    } else if (areIndexDefinitionsEquivalent(it_db_idx->second, model_idx_def, driverNameUpper)) {
                        qInfo() << "migrateManageIndexes: Index '" << QString::fromStdString(model_idx_def.index_name) << "' matches existing DB index. No changes.";
                        needs_create = false;
                    } else {
                        qInfo() << "migrateManageIndexes: Index '" << QString::fromStdString(model_idx_def.index_name) << "' exists but definition differs. Will DROP and RECREATE.";
                        needs_drop_first = true;
                    }
                }

                if (needs_drop_first) {
                    std::string drop_sql_std = "DROP INDEX " + QueryBuilder::quoteSqlIdentifier(model_idx_def.index_name);
                    if (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                        drop_sql_std += " ON " + QueryBuilder::quoteSqlIdentifier(meta.table_name);
                    }
                    drop_sql_std += ";";
                    qInfo() << "migrateManageIndexes (DROP DDL): " << QString::fromStdString(drop_sql_std);
                    auto [_, drop_err] = execute_ddl_query(session.getDbHandle(), drop_sql_std);
                    if (drop_err) {
                        qWarning() << "migrateManageIndexes: Failed to DROP index '" << QString::fromStdString(model_idx_def.index_name) << "': " << QString::fromStdString(drop_err.toString());
                        // If drop fails, we probably shouldn't proceed to create.
                        // However, for idempotency, if it's "index not found", that's okay.
                        // For now, continue to attempt creation.
                    }
                }

                if (needs_create) {
                    std::string cols_sql_part;
                    for (size_t i = 0; i < model_idx_def.db_column_names.size(); ++i) {
                        std::string col_name_for_index = model_idx_def.db_column_names[i];
                        std::string col_quoted_name = QueryBuilder::quoteSqlIdentifier(col_name_for_index);

                        const FieldMeta *field = meta.findFieldByDbName(col_name_for_index);
                        if (field && (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB")) {
                            bool needs_mysql_prefix = false;
                            if (!field->db_type_hint.empty()) {
                                std::string hint_upper = field->db_type_hint;
                                std::transform(hint_upper.begin(), hint_upper.end(), hint_upper.begin(), ::toupper);
                                if (hint_upper.find("TEXT") != std::string::npos || hint_upper.find("BLOB") != std::string::npos || hint_upper.find("JSON") != std::string::npos) {
                                    needs_mysql_prefix = true;
                                }
                                // Don't add prefix if hint is already VARCHAR(N)
                                if (hint_upper.rfind("VARCHAR(", 0) == 0) {
                                    needs_mysql_prefix = false;
                                }
                            } else {
                                std::string cpp_type_sql = Session::getSqlTypeForCppType(*field, driverNameUpper);
                                std::string cpp_type_sql_upper = cpp_type_sql;
                                std::transform(cpp_type_sql_upper.begin(), cpp_type_sql_upper.end(), cpp_type_sql_upper.begin(), ::toupper);
                                if (cpp_type_sql_upper.find("TEXT") != std::string::npos || cpp_type_sql_upper.find("BLOB") != std::string::npos || cpp_type_sql_upper.find("JSON") != std::string::npos) {
                                    needs_mysql_prefix = true;
                                }
                            }
                            if (needs_mysql_prefix) {
                                col_quoted_name += "(255)";
                            }
                        }
                        cols_sql_part += col_quoted_name;
                        if (i < model_idx_def.db_column_names.size() - 1) cols_sql_part += ", ";
                    }

                    std::string create_sql_std = "CREATE " + std::string(model_idx_def.is_unique ? "UNIQUE " : "") + "INDEX ";

                    create_sql_std += QueryBuilder::quoteSqlIdentifier(model_idx_def.index_name) + " ON " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " (" + cols_sql_part + ")";

                    if (!model_idx_def.type_str.empty() && (driverNameUpper == "QPSQL" || driverNameUpper == "POSTGRESQL" || driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB")) {
                        create_sql_std += " USING " + model_idx_def.type_str;
                    }
                    if (!model_idx_def.condition_str.empty() && (driverNameUpper == "QPSQL" || driverNameUpper == "POSTGRESQL")) {  // Partial index condition
                        create_sql_std += " WHERE (" + model_idx_def.condition_str + ")";
                    }
                    create_sql_std += ";";

                    qInfo() << "migrateManageIndexes (CREATE DDL): " << QString::fromStdString(create_sql_std);
                    auto [_, create_err] = execute_ddl_query(session.getDbHandle(), create_sql_std);
                    if (create_err) {
                        bool ignorable_already_exists_error = false;
                        std::string err_msg_lower = create_err.message;
                        std::transform(err_msg_lower.begin(), err_msg_lower.end(), err_msg_lower.begin(), ::tolower);

                        if (((driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") && create_err.native_db_error_code == 1061 /*ER_DUP_KEYNAME*/) ||
                            (driverNameUpper == "QSQLITE" && err_msg_lower.find("already exists") != std::string::npos) ||
                            ((driverNameUpper == "QPSQL" || driverNameUpper == "POSTGRESQL") && (create_err.sql_state == "42P07" /*duplicate_table (for index)*/ || create_err.sql_state == "42710" /*duplicate_object (general)*/))) {
                            ignorable_already_exists_error = true;
                        }

                        if (ignorable_already_exists_error && !needs_drop_first) {
                            qInfo() << "migrateManageIndexes: Index " << QString::fromStdString(model_idx_def.index_name) << " likely already exists (or an equivalent one with a different name if DB enforces uniqueness on columns): " << QString::fromStdString(create_err.toString());
                        } else if (!ignorable_already_exists_error) {
                            qWarning() << "migrateManageIndexes: Failed to CREATE index '" << QString::fromStdString(model_idx_def.index_name) << "': " << QString::fromStdString(create_err.toString());
                        }
                    }
                }
            }
            // Optionally drop indexes from DB that are not in model_index_names_processed
            for (const auto &[db_idx_name, db_idx_info] : existing_db_indexes) {
                if (db_idx_info.is_primary_key && (db_idx_name == "PRIMARY" || QString::fromStdString(db_idx_name).startsWith("sqlite_autoindex_"))) {  // Don't try to drop PKs or SQLite auto-indexes
                    continue;
                }
                if (model_index_names_processed.find(db_idx_name) == model_index_names_processed.end()) {
                    qInfo() << "migrateManageIndexes: Index '" << QString::fromStdString(db_idx_name) << "' exists in DB but not in model definition. Consider dropping it manually if no longer needed.";
                    // Example: Drop if needed (use with caution)
                    /*
                    std::string drop_extra_sql_std = "DROP INDEX " + QueryBuilder::quoteSqlIdentifier(db_idx_name);
                    if (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                        drop_extra_sql_std += " ON " + QueryBuilder::quoteSqlIdentifier(meta.table_name);
                    }
                    drop_extra_sql_std += ";";
                    qInfo() << "migrateManageIndexes (DROP EXTRA DDL): " << QString::fromStdString(drop_extra_sql_std);
                    auto [_, drop_extra_err] = execute_ddl_query(session.getDbHandle(), drop_extra_sql_std);
                    if (drop_extra_err) {
                        qWarning() << "migrateManageIndexes: Failed to DROP extra index '" << QString::fromStdString(db_idx_name) << "': " << QString::fromStdString(drop_extra_err.toString());
                    }
                    */
                }
            }
            return make_ok();
        }

    }  // namespace internal
}  // namespace cpporm