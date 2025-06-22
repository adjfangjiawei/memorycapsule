// Base/CppOrm/Include/cpporm/session_migrate_priv.h
#ifndef cpporm_SESSION_MIGRATE_PRIV_H
#define cpporm_SESSION_MIGRATE_PRIV_H

#include <QString>
#include <map>
#include <string>
#include <vector>

#include "cpporm/error.h"
#include "cpporm/model_base.h"  // For ModelMeta
// Removed algorithm and cctype, will be in the .cpp file with definition

namespace cpporm_sqldriver {
    class SqlDatabase;
    class SqlQuery;
}  // namespace cpporm_sqldriver

namespace cpporm {

    class Session;

    namespace internal {

        struct DbColumnInfo {
            std::string name;
            std::string type;
            std::string normalized_type;
            bool is_nullable = true;
            std::string default_value;
            std::string character_set_name;
            std::string collation_name;
            std::string column_key;  // e.g., "PRI", "UNI", "MUL"
            std::string extra;       // e.g., "auto_increment"
        };

        struct DbIndexInfo {
            std::string index_name;
            std::vector<std::string> column_names;
            bool is_unique = false;
            bool is_primary_key = false;
            std::string type_method;
        };

        // Declaration of normalizeDbType
        std::string normalizeDbType(const std::string &db_type_raw, const QString &driverNameUpperQ);

        // Declarations for functions in migrate_table_ops.cpp, migrate_column_ops.cpp, migrate_index_ops.cpp
        Error migrateCreateTable(Session &session, const ModelMeta &meta, const QString &driverNameUpper);
        std::map<std::string, DbColumnInfo> getTableColumnsInfo(Session &session, const QString &tableNameQString, const QString &driverNameUpper);
        Error migrateModifyColumns(Session &session, const ModelMeta &meta, const QString &driverNameUpper);
        std::map<std::string, DbIndexInfo> getTableIndexesInfo(Session &session, const QString &tableNameQString, const QString &driverNameUpper);
        Error migrateManageIndexes(Session &session, const ModelMeta &meta, const QString &driverNameUpper);
        bool areIndexDefinitionsEquivalent(const DbIndexInfo &db_idx, const IndexDefinition &model_idx_def, const QString &driverNameUpper);

        // Helper for executing DDL - already public static on Session, but good to have a consistent way if needed privately
        std::pair<cpporm_sqldriver::SqlQuery, Error> execute_ddl_query(cpporm_sqldriver::SqlDatabase &db, const std::string &ddl_sql_std);

    }  // namespace internal
}  // namespace cpporm

#endif  // cpporm_SESSION_MIGRATE_PRIV_H