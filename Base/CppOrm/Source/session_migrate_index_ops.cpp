// cpporm/session_migrate_index_ops.cpp
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_migrate_priv.h"

#include <QDebug>
#include <QRegularExpression> // For sanitizing names if used in auto-generated index names
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <algorithm>
#include <set> // For tracking processed names

namespace cpporm {
namespace internal {

// Helper to compare index definitions (columns and uniqueness)
// Order of columns in definition matters.
bool areIndexDefinitionsEquivalent(const DbIndexInfo &db_idx,
                                   const IndexDefinition &model_idx_def,
                                   const QString &driverNameUpper) {
  (void)driverNameUpper; // driverNameUpper might be used for type_method
                         // normalization later

  if (db_idx.is_unique != model_idx_def.is_unique)
    return false;
  if (db_idx.column_names.size() != model_idx_def.db_column_names.size())
    return false;

  // Assuming column names from DB and model are already in canonical form
  // (e.g., lowercase or as defined) If not, normalization (e.g., to_lower)
  // should happen before comparison. Direct comparison of ordered columns:
  for (size_t i = 0; i < db_idx.column_names.size(); ++i) {
    // Basic case-insensitive comparison for safety, though DBs might be
    // case-sensitive
    std::string db_col_lower = db_idx.column_names[i];
    std::string model_col_lower = model_idx_def.db_column_names[i];
    std::transform(db_col_lower.begin(), db_col_lower.end(),
                   db_col_lower.begin(), ::tolower);
    std::transform(model_col_lower.begin(), model_col_lower.end(),
                   model_col_lower.begin(), ::tolower);
    if (db_col_lower != model_col_lower)
      return false;
  }

  // Compare index type/method (BTREE, HASH, GIN, etc.) if available and
  // specified
  if (!model_idx_def.type_str.empty() && !db_idx.type_method.empty()) {
    std::string model_type_lower = model_idx_def.type_str;
    std::string db_type_lower = db_idx.type_method;
    std::transform(model_type_lower.begin(), model_type_lower.end(),
                   model_type_lower.begin(), ::tolower);
    std::transform(db_type_lower.begin(), db_type_lower.end(),
                   db_type_lower.begin(), ::tolower);
    // Simple string comparison. More robust would be to normalize (e.g. "btree"
    // vs "BTREE")
    if (model_type_lower != db_type_lower) {
      // Exception: MySQL often reports "BTREE" by default, model might not
      // specify it. If model type_str is empty, but DB has a common default
      // like BTREE, consider it a match.
      if (!model_idx_def.type_str
               .empty()) { // Only if model explicitly specifies a type
        return false;
      }
    }
  }
  // Compare partial index conditions (mainly PG) - this is complex
  // if (!model_idx_def.condition_str.empty() || (db_idx has condition property
  // and it's not empty)) {
  //    if (normalize_condition(model_idx_def.condition_str) !=
  //    normalize_condition(db_idx.condition_property)) return false;
  // }

  return true;
}

std::map<std::string, DbIndexInfo>
getTableIndexesInfo(Session &session, const QString &tableName,
                    const QString &driverNameUpper) {
  std::map<std::string, DbIndexInfo> indexes;
  QSqlQuery query(session.getDbHandle());
  QString sql;

  if (driverNameUpper == "QSQLITE") {
    if (!query.exec(QString("PRAGMA index_list(%1);").arg(tableName))) {
      qWarning() << "getTableIndexesInfo (SQLite): Failed to get index list for"
                 << tableName << ":" << query.lastError().text();
      return indexes;
    }
    std::vector<DbIndexInfo> temp_index_list_sqlite;
    while (query.next()) {
      DbIndexInfo idx_base_info;
      idx_base_info.index_name = query.value("name").toString().toStdString();
      idx_base_info.is_unique = query.value("unique").toInt() == 1;
      // "origin" column can tell if 'pk', 'u' (unique constraint), 'c' (create
      // index) We want to manage 'c' type indexes primarily. PKs and UNIQUE
      // constraints are handled by table def.
      QString origin = query.value("origin").toString();
      if (idx_base_info.index_name.starts_with("sqlite_autoindex_") ||
          origin == "pk" || origin == "u") {
        idx_base_info.is_primary_key =
            (origin == "pk"); // Mark it if it's an autoindex for PK
        // Store it to avoid recreating, but don't manage as separate index if
        // it's just backing a constraint indexes[idx_base_info.index_name] =
        // idx_base_info;
        continue; // Skip auto-indexes and those backing PK/UNIQUE constraints
                  // for explicit management
      }
      temp_index_list_sqlite.push_back(idx_base_info);
    }

    for (DbIndexInfo &idx_info_ref : temp_index_list_sqlite) {
      if (!query.exec(QString("PRAGMA index_xinfo(%1);")
                          .arg(QString::fromStdString(
                              idx_info_ref.index_name)))) { // index_xinfo for
                                                            // column names
        if (!query.exec(
                QString("PRAGMA index_info(%1);")
                    .arg(QString::fromStdString(idx_info_ref.index_name)))) {
          qWarning()
              << "getTableIndexesInfo (SQLite): Failed to get info for index"
              << QString::fromStdString(idx_info_ref.index_name) << ":"
              << query.lastError().text();
          continue;
        }
      }
      std::vector<std::pair<int, std::string>>
          col_order_pairs; // cid for xinfo, seqno for info
      bool use_cid = query.record().contains("cid");

      while (query.next()) {
        col_order_pairs.push_back({
            query.value(use_cid ? "cid" : "seqno").toInt(),
            query.value("name").toString().toStdString() // This is the column
                                                         // name
        });
      }
      std::sort(col_order_pairs.begin(),
                col_order_pairs.end()); // Sort by sequence number
      for (const auto &p : col_order_pairs)
        idx_info_ref.column_names.push_back(p.second);

      if (!idx_info_ref.column_names.empty())
        indexes[idx_info_ref.index_name] = idx_info_ref;
    }

  } else if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
    sql = QString("SHOW INDEX FROM %1;")
              .arg(QString::fromStdString(
                  QueryBuilder::quoteSqlIdentifier(tableName.toStdString())));
    if (!query.exec(sql)) {
      qWarning() << "getTableIndexesInfo (MySQL): Failed for table" << tableName
                 << ":" << query.lastError().text();
      return indexes;
    }

    std::map<std::string, DbIndexInfo> temp_building_indexes;
    while (query.next()) {
      std::string idx_name_str =
          query.value("Key_name").toString().toStdString();
      DbIndexInfo &current_idx =
          temp_building_indexes[idx_name_str]; // Creates if not exists

      if (current_idx.index_name.empty()) { // First time seeing this index name
        current_idx.index_name = idx_name_str;
        current_idx.is_unique = (query.value("Non_unique").toInt() == 0);
        current_idx.is_primary_key = (idx_name_str == "PRIMARY");
        current_idx.type_method =
            query.value("Index_type").toString().toStdString();
      }
      // Add column in its sequence
      // SHOW INDEX already returns columns ordered by Seq_in_index for a given
      // Key_name
      current_idx.column_names.push_back(
          query.value("Column_name").toString().toStdString());
    }
    for (const auto &pair_val : temp_building_indexes) {
      if (pair_val.second.is_primary_key)
        continue; // Skip managing PRIMARY KEY as an index here
      if (!pair_val.second.column_names.empty())
        indexes[pair_val.first] = pair_val.second;
    }

  } else if (driverNameUpper == "QPSQL") {
    sql =
        QString("SELECT idx.relname AS index_name, att.attname AS column_name, "
                "i.indisunique AS is_unique, "
                "       i.indisprimary AS is_primary, am.amname AS index_type, "
                "       (SELECT pg_get_indexdef(i.indexrelid, k + 1, true) "
                "FROM generate_subscripts(i.indkey, 1) k WHERE i.indkey[k] = "
                "att.attnum) as col_def_in_idx, "
                "       array_position(i.indkey, att.attnum) as column_seq "
                "FROM   pg_index i "
                "JOIN   pg_class tbl ON tbl.oid = i.indrelid "
                "JOIN   pg_class idx ON idx.oid = i.indexrelid "
                "JOIN   pg_attribute att ON att.attrelid = tbl.oid AND "
                "att.attnum = ANY(i.indkey) "
                "LEFT JOIN pg_am am ON am.oid = idx.relam " // Use LEFT JOIN for
                                                            // pg_am
                "WHERE  tbl.relname = '%1' AND tbl.relnamespace = (SELECT oid "
                "FROM pg_namespace WHERE nspname = current_schema()) "
                "ORDER BY index_name, column_seq;" // Order for correct column
                                                   // assembly
                )
            .arg(tableName);

    if (!query.exec(sql)) {
      qWarning() << "getTableIndexesInfo (PostgreSQL): Failed for table"
                 << tableName << ":" << query.lastError().text()
                 << "SQL:" << sql;
      return indexes;
    }

    std::map<std::string, DbIndexInfo> temp_building_indexes_pg;
    while (query.next()) {
      std::string idx_name_str =
          query.value("index_name").toString().toStdString();
      DbIndexInfo &current_idx = temp_building_indexes_pg[idx_name_str];

      if (current_idx.index_name.empty()) {
        current_idx.index_name = idx_name_str;
        current_idx.is_unique = query.value("is_unique").toBool();
        current_idx.is_primary_key = query.value("is_primary").toBool();
        current_idx.type_method =
            query.value("index_type").toString().toStdString();
      }
      // pg_get_indexdef can give more info about expression indexes or
      // collation/opclass For simple column names, att.attname is fine.
      current_idx.column_names.push_back(
          query.value("column_name").toString().toStdString());
    }
    for (const auto &pair_val : temp_building_indexes_pg) {
      if (pair_val.second.is_primary_key)
        continue;
      if (!pair_val.second.column_names.empty())
        indexes[pair_val.first] = pair_val.second;
    }
  } else {
    qWarning() << "getTableIndexesInfo: Unsupported driver for index info:"
               << driverNameUpper;
  }
  return indexes;
}

Error migrateManageIndexes(Session &session, const ModelMeta &meta,
                           const QString &driverNameUpper) {
  qInfo() << "migrateManageIndexes: Managing indexes for table '"
          << QString::fromStdString(meta.table_name) << "'...";
  std::map<std::string, DbIndexInfo> existing_db_indexes = getTableIndexesInfo(
      session, QString::fromStdString(meta.table_name), driverNameUpper);

  std::set<std::string>
      model_index_names_processed; // To track which model indexes we've handled

  // Pass 1: Ensure indexes defined in ModelMeta exist and match, or
  // create/recreate them.
  for (const auto &model_idx_def : meta.indexes) {
    if (model_idx_def.db_column_names.empty()) {
      qWarning() << "migrateManageIndexes: Model index definition for table '"
                 << QString::fromStdString(meta.table_name)
                 << "' (intended name: '"
                 << QString::fromStdString(model_idx_def.index_name)
                 << "') has no columns. Skipping.";
      continue;
    }

    QString model_idx_name_qstr =
        QString::fromStdString(model_idx_def.index_name);
    if (model_idx_name_qstr.isEmpty()) {
      model_idx_name_qstr = (model_idx_def.is_unique ? "uix_" : "idx_") +
                            QString::fromStdString(meta.table_name);
      for (const auto &col_name_std : model_idx_def.db_column_names) {
        QString temp_col_name = QString::fromStdString(col_name_std);
        temp_col_name.replace(QRegularExpression("[^a-zA-Z0-9_]"), "_");
        model_idx_name_qstr += "_" + temp_col_name;
      }
      if (model_idx_name_qstr.length() >
          60) { // Basic length check for compatibility
        QString hash_suffix =
            QString::number(qHash(model_idx_name_qstr +
                                  QString::number(model_idx_def.is_unique)),
                            16)
                .left(8);
        model_idx_name_qstr =
            model_idx_name_qstr.left(60 - 1 - hash_suffix.length()) + "_" +
            hash_suffix;
      }
    }
    std::string model_idx_name_std = model_idx_name_qstr.toStdString();
    model_index_names_processed.insert(model_idx_name_std);

    auto it_db_idx = existing_db_indexes.find(model_idx_name_std);
    bool needs_create = true;
    bool needs_drop_first = false;

    if (it_db_idx != existing_db_indexes.end()) {
      if (it_db_idx->second.is_primary_key) {
        qInfo()
            << "migrateManageIndexes: Model index '" << model_idx_name_qstr
            << "' matches a DB PRIMARY KEY. Explicit index management skipped.";
        needs_create = false;
      } else if (areIndexDefinitionsEquivalent(it_db_idx->second, model_idx_def,
                                               driverNameUpper)) {
        qInfo() << "migrateManageIndexes: Index '" << model_idx_name_qstr
                << "' matches existing DB index. No changes.";
        needs_create = false;
      } else {
        qInfo() << "migrateManageIndexes: Index '" << model_idx_name_qstr
                << "' exists but definition differs. Will DROP and RECREATE.";
        needs_drop_first = true;
        needs_create = true;
      }
    }

    if (needs_drop_first) {
      QString drop_sql =
          QString("DROP INDEX %1")
              .arg(QString::fromStdString(
                  QueryBuilder::quoteSqlIdentifier(model_idx_name_std)));
      if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
        drop_sql +=
            " ON " + QString::fromStdString(
                         QueryBuilder::quoteSqlIdentifier(meta.table_name));
      }
      drop_sql += ";";
      qInfo() << "migrateManageIndexes (DROP DDL): " << drop_sql;
      auto [_, drop_err] =
          execute_ddl_query(session.getDbHandle(), drop_sql); // Pass copy
      if (drop_err) {
        qWarning() << "migrateManageIndexes: Failed to DROP index '"
                   << model_idx_name_qstr
                   << "': " << QString::fromStdString(drop_err.toString());
      }
    }

    if (needs_create) {
      QStringList q_cols;
      for (const auto &c : model_idx_def.db_column_names)
        q_cols << QString::fromStdString(QueryBuilder::quoteSqlIdentifier(c));

      QString create_sql =
          QString("CREATE %1INDEX %2%3 ON %4 (%5)")
              .arg(model_idx_def.is_unique ? "UNIQUE " : "")
              .arg((driverNameUpper == "QSQLITE" || driverNameUpper == "QPSQL")
                       ? "IF NOT EXISTS "
                       : "")
              .arg(QString::fromStdString(
                  QueryBuilder::quoteSqlIdentifier(model_idx_name_std)))
              .arg(QString::fromStdString(
                  QueryBuilder::quoteSqlIdentifier(meta.table_name)))
              .arg(q_cols.join(", "));
      if (!model_idx_def.type_str.empty() &&
          (driverNameUpper == "QPSQL" || driverNameUpper == "QMYSQL" ||
           driverNameUpper == "QMARIADB")) {
        create_sql +=
            " USING " + QString::fromStdString(model_idx_def.type_str);
      }
      if (!model_idx_def.condition_str.empty() && driverNameUpper == "QPSQL") {
        create_sql += " WHERE (" +
                      QString::fromStdString(model_idx_def.condition_str) + ")";
      }
      create_sql += ";";

      qInfo() << "migrateManageIndexes (CREATE DDL): " << create_sql;
      auto [_, create_err] =
          execute_ddl_query(session.getDbHandle(), create_sql); // Pass copy
      if (create_err) {
        bool ignorable = false;
        std::string err_msg_lower = create_err.message;
        std::transform(err_msg_lower.begin(), err_msg_lower.end(),
                       err_msg_lower.begin(), ::tolower);

        if (((driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") &&
             create_err.native_db_error_code == 1061 /*ER_DUP_KEYNAME*/) ||
            (driverNameUpper == "QSQLITE" &&
             err_msg_lower.find("already exists") != std::string::npos) ||
            (driverNameUpper == "QPSQL" && (create_err.sql_state == "42P07" ||
                                            create_err.sql_state == "42710"))) {
          ignorable = true;
        }

        if (ignorable && !needs_drop_first) { // Only truly ignorable if we
                                              // didn't try to drop it first
          qInfo() << "migrateManageIndexes: Index " << model_idx_name_qstr
                  << " likely already exists (error caught on create): "
                  << QString::fromStdString(create_err.toString());
        } else if (!ignorable) { // If it's not an "already exists" error, or if
                                 // we tried to drop and still failed create
          qWarning() << "migrateManageIndexes: Failed to CREATE index '"
                     << model_idx_name_qstr
                     << "': " << QString::fromStdString(create_err.toString());
        }
      }
    }
  }
  return make_ok();
}

} // namespace internal
} // namespace cpporm