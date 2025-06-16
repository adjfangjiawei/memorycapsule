// cpporm/session_migrate_priv.h
#ifndef cpporm_SESSION_MIGRATE_PRIV_H
#define cpporm_SESSION_MIGRATE_PRIV_H

#include "cpporm/error.h"
#include "cpporm/model_base.h"
#include <QSqlDatabase>
#include <QString>
#include <map>
#include <string>
#include <vector>

namespace cpporm {

class Session;

namespace internal {

// --- Table Operations ---
Error migrateCreateTable(Session &session, const ModelMeta &meta,
                         const QString &driverNameUpper);

// --- Column Operations ---
struct DbColumnInfo {
  std::string name;
  std::string type;
  std::string normalized_type;
  bool is_nullable = true;
  std::string default_value;
  std::string character_set_name;
  std::string collation_name;
  std::string column_key;
  std::string extra;
};
std::map<std::string, DbColumnInfo>
getTableColumnsInfo(Session &session, const QString &tableName,
                    const QString &driverNameUpper);
Error migrateModifyColumns(Session &session, const ModelMeta &meta,
                           const QString &driverNameUpper);

// --- Index Operations ---
struct DbIndexInfo {
  std::string index_name;
  std::vector<std::string> column_names;
  bool is_unique = false;
  bool is_primary_key = false;
  std::string type_method;
};
std::map<std::string, DbIndexInfo>
getTableIndexesInfo(Session &session, const QString &tableName,
                    const QString &driverNameUpper);
Error migrateManageIndexes(Session &session, const ModelMeta &meta,
                           const QString &driverNameUpper);

// Helper for executing DDL - now takes QSqlDatabase by value (copy)
std::pair<QSqlQuery, Error> execute_ddl_query(QSqlDatabase db,
                                              const QString &ddl_sql);

} // namespace internal
} // namespace cpporm

#endif // cpporm_SESSION_MIGRATE_PRIV_H