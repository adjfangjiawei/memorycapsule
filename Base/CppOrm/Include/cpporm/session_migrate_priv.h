// Base/CppOrm/Include/cpporm/session_migrate_priv.h
#ifndef cpporm_SESSION_MIGRATE_PRIV_H
#define cpporm_SESSION_MIGRATE_PRIV_H

#include "cpporm/error.h"
#include "cpporm/model_base.h"
// #include <QSqlDatabase> // Removed: No longer needed here, functions use Session&
#include <QString>  // Still used for driverNameUpper and some internal logic if not fully migrated
#include <map>
#include <string>
#include <vector>

// Forward declare SqlDatabase if needed, but Session reference should provide it
namespace cpporm_sqldriver {
    class SqlDatabase;
    class SqlQuery;
}  // namespace cpporm_sqldriver

namespace cpporm {

    class Session;

    namespace internal {

        // --- Table Operations ---
        Error migrateCreateTable(Session &session, const ModelMeta &meta, const QString &driverNameUpper);

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
        std::map<std::string, DbColumnInfo> getTableColumnsInfo(Session &session, const QString &tableName, const QString &driverNameUpper);
        Error migrateModifyColumns(Session &session, const ModelMeta &meta, const QString &driverNameUpper);

        // --- Index Operations ---
        struct DbIndexInfo {
            std::string index_name;
            std::vector<std::string> column_names;
            bool is_unique = false;
            bool is_primary_key = false;
            std::string type_method;
        };
        std::map<std::string, DbIndexInfo> getTableIndexesInfo(Session &session, const QString &tableName, const QString &driverNameUpper);
        Error migrateManageIndexes(Session &session, const ModelMeta &meta, const QString &driverNameUpper);

        // Helper for executing DDL - signature changed
        std::pair<cpporm_sqldriver::SqlQuery, Error> execute_ddl_query(cpporm_sqldriver::SqlDatabase &db,  // Pass by reference
                                                                       const std::string &ddl_sql_std);    // SQL as std::string

    }  // namespace internal
}  // namespace cpporm

#endif  // cpporm_SESSION_MIGRATE_PRIV_H