// Base/CppOrm/Source/session_migrate_index_ops.cpp
#include <QDebug>
#include <QRegularExpression>  // For QHash in auto_name_str generation if needed
#include <QString>             // For QString::fromStdString, qHash etc.
#include <algorithm>
#include <set>

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

        bool areIndexDefinitionsEquivalent(const DbIndexInfo &db_idx, const IndexDefinition &model_idx_def, const QString &driverNameUpper);

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
                    idx_base_info.index_name = query.value(rec_meta_idxlist.indexOf("name")).toString();  // Already std::string
                    idx_base_info.is_unique = query.value(rec_meta_idxlist.indexOf("unique")).toInt32() == 1;
                    std::string origin = query.value(rec_meta_idxlist.indexOf("origin")).toString();  // Already std::string
                    // Use QString::fromStdString for startsWith
                    if (QString::fromStdString(idx_base_info.index_name).startsWith("sqlite_autoindex_") || origin == "pk" || origin == "u") {
                        idx_base_info.is_primary_key = (origin == "pk");
                        // Skip SQLite auto indexes and PK/Unique constraint indexes as they are not managed explicitly as "CREATE INDEX"
                        continue;
                    }
                    temp_index_list_sqlite.push_back(idx_base_info);
                }

                for (DbIndexInfo &idx_info_ref : temp_index_list_sqlite) {
                    std::string idx_info_sql_std = "PRAGMA index_xinfo(" + QueryBuilder::quoteSqlIdentifier(idx_info_ref.index_name) + ");";
                    if (!query.exec(idx_info_sql_std)) {
                        idx_info_sql_std = "PRAGMA index_info(" + QueryBuilder::quoteSqlIdentifier(idx_info_ref.index_name) + ");";
                        if (!query.exec(idx_info_sql_std)) {
                            qWarning() << "getTableIndexesInfo (SQLite): Failed to get info for index" << QString::fromStdString(idx_info_ref.index_name) << ":" << QString::fromStdString(query.lastError().text());
                            continue;
                        }
                    }
                    std::vector<std::pair<int, std::string>> col_order_pairs;
                    cpporm_sqldriver::SqlRecord rec_meta_idxinfo = query.recordMetadata();
                    bool use_cid = rec_meta_idxinfo.contains("cid");  // Check if "cid" column exists for newer PRAGMA

                    while (query.next()) {
                        // "name" field from index_info/index_xinfo is the column name, can be null for expressions
                        cpporm_sqldriver::SqlValue col_name_val = query.value(rec_meta_idxinfo.indexOf("name"));
                        std::string col_name_str = col_name_val.isNull() ? "" : col_name_val.toString();
                        if (!col_name_str.empty()) {  // Only add if column name is not null
                            col_order_pairs.push_back({query.value(rec_meta_idxinfo.indexOf(use_cid ? "cid" : "seqno")).toInt32(), col_name_str});
                        }
                    }
                    std::sort(col_order_pairs.begin(), col_order_pairs.end());
                    for (const auto &p : col_order_pairs) idx_info_ref.column_names.push_back(p.second);

                    if (!idx_info_ref.column_names.empty()) indexes[idx_info_ref.index_name] = idx_info_ref;
                }

            } else if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                sql_std = "SHOW INDEX FROM " + QueryBuilder::quoteSqlIdentifier(tableNameStd) + ";";
                if (!query.exec(sql_std)) {
                    qWarning() << "getTableIndexesInfo (MySQL): Failed for table" << tableNameQString << ":" << QString::fromStdString(query.lastError().text());
                    return indexes;
                }

                std::map<std::string, DbIndexInfo> temp_building_indexes;
                cpporm_sqldriver::SqlRecord rec_meta_mysql = query.recordMetadata();
                while (query.next()) {
                    std::string idx_name_str = query.value(rec_meta_mysql.indexOf("Key_name")).toString();  // Already std::string
                    DbIndexInfo &current_idx_ref = temp_building_indexes[idx_name_str];                     // Corrected: use &

                    if (current_idx_ref.index_name.empty()) {  // First time seeing this index name
                        current_idx_ref.index_name = idx_name_str;
                        current_idx_ref.is_unique = (query.value(rec_meta_mysql.indexOf("Non_unique")).toInt32() == 0);
                        current_idx_ref.is_primary_key = (idx_name_str == "PRIMARY");
                        current_idx_ref.type_method = query.value(rec_meta_mysql.indexOf("Index_type")).toString();  // Already std::string
                    }
                    current_idx_ref.column_names.push_back(query.value(rec_meta_mysql.indexOf("Column_name")).toString());  // Already std::string
                }
                for (const auto &pair_val : temp_building_indexes) {
                    if (pair_val.second.is_primary_key) continue;  // Skip PRIMARY KEY, managed by table creation
                    if (!pair_val.second.column_names.empty()) indexes[pair_val.first] = pair_val.second;
                }

            } else if (driverNameUpper == "QPSQL") {
                // Query for PostgreSQL can be complex to get all details (like sort order, opclass)
                // Simplified version for column names, uniqueness, and type:
                sql_std =
                    "SELECT idx.relname AS index_name, att.attname AS column_name, "
                    "i.indisunique AS is_unique, "
                    "i.indisprimary AS is_primary, am.amname AS index_type, "
                    "array_position(i.indkey, att.attnum) as column_seq "  // Used for ordering columns
                    "FROM   pg_index i "
                    "JOIN   pg_class tbl ON tbl.oid = i.indrelid "
                    "JOIN   pg_class idx ON idx.oid = i.indexrelid "
                    "JOIN   pg_attribute att ON att.attrelid = tbl.oid AND att.attnum = ANY(i.indkey) "
                    "LEFT JOIN pg_am am ON am.oid = idx.relam "
                    "WHERE  tbl.relname = '" +
                    tableNameStd +  // Table name as string literal
                    "' AND tbl.relnamespace = (SELECT oid FROM pg_namespace WHERE nspname = current_schema()) "
                    "ORDER BY index_name, column_seq;";  // Order by seq to get columns in correct order
                if (!query.exec(sql_std)) {
                    qWarning() << "getTableIndexesInfo (PostgreSQL): Failed for table" << tableNameQString << ":" << QString::fromStdString(query.lastError().text()) << "SQL:" << QString::fromStdString(sql_std);
                    return indexes;
                }

                std::map<std::string, DbIndexInfo> temp_building_indexes_pg;
                cpporm_sqldriver::SqlRecord rec_meta_pg = query.recordMetadata();
                while (query.next()) {
                    std::string idx_name_str = query.value(rec_meta_pg.indexOf("index_name")).toString();  // Already std::string
                    DbIndexInfo &current_idx_ref = temp_building_indexes_pg[idx_name_str];                 // Corrected: use &

                    if (current_idx_ref.index_name.empty()) {  // First time seeing this index name
                        current_idx_ref.index_name = idx_name_str;
                        current_idx_ref.is_unique = query.value(rec_meta_pg.indexOf("is_unique")).toBool();
                        current_idx_ref.is_primary_key = query.value(rec_meta_pg.indexOf("is_primary")).toBool();
                        current_idx_ref.type_method = query.value(rec_meta_pg.indexOf("index_type")).toString();  // Already std::string
                    }
                    current_idx_ref.column_names.push_back(query.value(rec_meta_pg.indexOf("column_name")).toString());  // Already std::string
                }
                for (const auto &pair_val : temp_building_indexes_pg) {
                    if (pair_val.second.is_primary_key) continue;  // Skip PRIMARY KEY
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

            std::set<std::string> model_index_names_processed;

            for (const auto &model_idx_def_const : meta.indexes) {
                IndexDefinition model_idx_def = model_idx_def_const;  // Make a mutable copy
                if (model_idx_def.db_column_names.empty()) {
                    qWarning() << "migrateManageIndexes: Model index definition for table '" << QString::fromStdString(meta.table_name) << "' (intended name: '" << QString::fromStdString(model_idx_def.index_name) << "') has no columns. Skipping.";
                    continue;
                }

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
                    // Basic length check and hash for very long names (common in some DBs like Oracle, PG)
                    // Max identifier length varies by DB (e.g., PG default 63, MySQL 64)
                    if (auto_name_str.length() > 60) {  // Arbitrary limit, adjust as needed
                        QString q_auto_name_str = QString::fromStdString(auto_name_str);
                        // Using qHash for a simple hash.
                        QString hash_suffix = QString::number(qHash(q_auto_name_str + QString::number(model_idx_def.is_unique)), 16).left(8);
                        auto_name_str = q_auto_name_str.left(60 - 1 - hash_suffix.length()).toStdString() + "_" + hash_suffix.toStdString();
                    }
                    model_idx_def.index_name = auto_name_str;
                }
                model_index_names_processed.insert(model_idx_def.index_name);

                auto it_db_idx = existing_db_indexes.find(model_idx_def.index_name);
                bool needs_create = true;
                bool needs_drop_first = false;

                if (it_db_idx != existing_db_indexes.end()) {
                    // If DB index is a PK index, and model defines it as non-PK, or vice-versa, might be an issue.
                    // For simplicity, if DB index is PK, we generally don't touch it via CREATE INDEX/DROP INDEX.
                    if (it_db_idx->second.is_primary_key) {
                        qInfo() << "migrateManageIndexes: Model index '" << QString::fromStdString(model_idx_def.index_name) << "' matches a DB PRIMARY KEY. Explicit index management skipped.";
                        needs_create = false;
                    } else if (areIndexDefinitionsEquivalent(it_db_idx->second, model_idx_def, driverNameUpper)) {
                        qInfo() << "migrateManageIndexes: Index '" << QString::fromStdString(model_idx_def.index_name) << "' matches existing DB index. No changes.";
                        needs_create = false;
                    } else {
                        qInfo() << "migrateManageIndexes: Index '" << QString::fromStdString(model_idx_def.index_name) << "' exists but definition differs. Will DROP and RECREATE.";
                        needs_drop_first = true;
                        needs_create = true;  // Still need to create after drop
                    }
                }
                // If it_db_idx == existing_db_indexes.end(), it's a new index, needs_create remains true.

                if (needs_drop_first) {
                    std::string drop_sql_std = "DROP INDEX " + QueryBuilder::quoteSqlIdentifier(model_idx_def.index_name);
                    if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                        drop_sql_std += " ON " + QueryBuilder::quoteSqlIdentifier(meta.table_name);
                    }
                    drop_sql_std += ";";
                    qInfo() << "migrateManageIndexes (DROP DDL): " << QString::fromStdString(drop_sql_std);
                    auto [_, drop_err] = execute_ddl_query(session.getDbHandle(), drop_sql_std);
                    if (drop_err) {
                        // Log but continue, maybe it didn't exist or drop failed for other reason
                        qWarning() << "migrateManageIndexes: Failed to DROP index '" << QString::fromStdString(model_idx_def.index_name) << "': " << QString::fromStdString(drop_err.toString());
                    }
                }

                if (needs_create) {
                    std::string cols_sql_part;
                    for (size_t i = 0; i < model_idx_def.db_column_names.size(); ++i) {
                        cols_sql_part += QueryBuilder::quoteSqlIdentifier(model_idx_def.db_column_names[i]);
                        if (i < model_idx_def.db_column_names.size() - 1) cols_sql_part += ", ";
                    }

                    std::string create_sql_std = "CREATE " + std::string(model_idx_def.is_unique ? "UNIQUE " : "") + "INDEX ";
                    if (driverNameUpper == "QSQLITE" || driverNameUpper == "QPSQL") {
                        // Add IF NOT EXISTS for SQLite and PostgreSQL to avoid error if drop failed or race condition
                        create_sql_std += "IF NOT EXISTS ";
                    }
                    create_sql_std += QueryBuilder::quoteSqlIdentifier(model_idx_def.index_name) + " ON " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " (" + cols_sql_part + ")";

                    if (!model_idx_def.type_str.empty() && (driverNameUpper == "QPSQL" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB")) {
                        create_sql_std += " USING " + model_idx_def.type_str;  // e.g., USING HASH, USING BTREE
                    }
                    if (!model_idx_def.condition_str.empty() && driverNameUpper == "QPSQL") {  // Partial index condition
                        create_sql_std += " WHERE (" + model_idx_def.condition_str + ")";
                    }
                    create_sql_std += ";";

                    qInfo() << "migrateManageIndexes (CREATE DDL): " << QString::fromStdString(create_sql_std);
                    auto [_, create_err] = execute_ddl_query(session.getDbHandle(), create_sql_std);
                    if (create_err) {
                        // Check if error is "already exists" and if we didn't intend to drop first (e.g. initial migration)
                        bool ignorable_already_exists_error = false;
                        std::string err_msg_lower = create_err.message;  // Assuming Error::message is std::string
                        std::transform(err_msg_lower.begin(), err_msg_lower.end(), err_msg_lower.begin(), ::tolower);

                        if (((driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") && create_err.native_db_error_code == 1061 /*ER_DUP_KEYNAME*/) || (driverNameUpper == "QSQLITE" && err_msg_lower.find("already exists") != std::string::npos) ||
                            (driverNameUpper == "QPSQL" && (create_err.sql_state == "42P07" /*duplicate_table (for relations)*/ || create_err.sql_state == "42710" /*duplicate_object (for indexes on PG >= 9.5)*/))) {
                            ignorable_already_exists_error = true;
                        }

                        if (ignorable_already_exists_error && !needs_drop_first) {
                            qInfo() << "migrateManageIndexes: Index " << QString::fromStdString(model_idx_def.index_name) << " likely already exists (error caught on create): " << QString::fromStdString(create_err.toString());
                        } else if (!ignorable_already_exists_error) {  // Report non-ignorable errors
                            qWarning() << "migrateManageIndexes: Failed to CREATE index '" << QString::fromStdString(model_idx_def.index_name) << "': " << QString::fromStdString(create_err.toString());
                        }
                    }
                }
            }

            // Optionally, drop DB indexes that are not in the model definition
            // This part is commented out as it can be destructive. Enable with caution.
            /*
            for (const auto& db_idx_pair : existing_db_indexes) {
                if (db_idx_pair.second.is_primary_key) continue; // Don't drop PKs
                if (model_index_names_processed.find(db_idx_pair.first) == model_index_names_processed.end()) {
                    qInfo() << "migrateManageIndexes: Index '" << QString::fromStdString(db_idx_pair.first) << "' exists in DB but not in model. Dropping.";
                    std::string drop_sql_std = "DROP INDEX " + QueryBuilder::quoteSqlIdentifier(db_idx_pair.first);
                    if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                        drop_sql_std += " ON " + QueryBuilder::quoteSqlIdentifier(meta.table_name);
                    }
                    drop_sql_std += ";";
                    qInfo() << "migrateManageIndexes (Orphan DROP DDL): " << QString::fromStdString(drop_sql_std);
                    auto [_, drop_err] = execute_ddl_query(session.getDbHandle(), drop_sql_std);
                    if (drop_err) {
                        qWarning() << "migrateManageIndexes: Failed to DROP orphan index '" << QString::fromStdString(db_idx_pair.first) << "': " << QString::fromStdString(drop_err.toString());
                    }
                }
            }
            */
            return make_ok();
        }

        bool areIndexDefinitionsEquivalent(const DbIndexInfo &db_idx, const IndexDefinition &model_idx_def, const QString &driverNameUpper) {
            (void)driverNameUpper;  // driverNameUpper might be used for more nuanced comparison later

            // Compare uniqueness
            if (db_idx.is_unique != model_idx_def.is_unique) return false;

            // Compare column names (order matters, case-insensitivity for names if DB is case-insensitive)
            if (db_idx.column_names.size() != model_idx_def.db_column_names.size()) return false;
            for (size_t i = 0; i < db_idx.column_names.size(); ++i) {
                std::string db_col_lower = db_idx.column_names[i];
                std::string model_col_lower = model_idx_def.db_column_names[i];
                std::transform(db_col_lower.begin(), db_col_lower.end(), db_col_lower.begin(), ::tolower);
                std::transform(model_col_lower.begin(), model_col_lower.end(), model_col_lower.begin(), ::tolower);
                if (db_col_lower != model_col_lower) return false;
            }

            // Compare index type/method if both are specified (case-insensitive)
            if (!model_idx_def.type_str.empty() && !db_idx.type_method.empty()) {
                std::string model_type_lower = model_idx_def.type_str;
                std::string db_type_lower = db_idx.type_method;
                std::transform(model_type_lower.begin(), model_type_lower.end(), model_type_lower.begin(), ::tolower);
                std::transform(db_type_lower.begin(), db_type_lower.end(), db_type_lower.begin(), ::tolower);
                if (model_type_lower != db_type_lower) {
                    // Allow some flexibility e.g. model says "BTREE" but DB reports "btree"
                    // This is handled by tolower. If still different, and model specified a type, they are different.
                    if (!model_idx_def.type_str.empty()) {  // Only consider a mismatch if model explicitly set a type
                        return false;
                    }
                }
            } else if (!model_idx_def.type_str.empty() && db_idx.type_method.empty()) {
                // Model specifies a type, DB does not report one (or reports default which was filtered out)
                // This could be a difference if the DB default type is not what model expects
                // For simplicity, if model specifies a type, DB must match it or be considered different.
                // This might need refinement based on driver behavior.
                return false;
            }
            // Note: Index condition (model_idx_def.condition_str) is not compared here.
            // Full comparison would require parsing DB index definition or more detailed DB schema queries.
            return true;
        }

    }  // namespace internal
}  // namespace cpporm