// Base/CppOrm/Include/cpporm/session_migrate_priv.h
#ifndef cpporm_SESSION_MIGRATE_PRIV_H
#define cpporm_SESSION_MIGRATE_PRIV_H

#include <QString>
#include <map>
#include <string>
#include <vector>

#include "cpporm/error.h"
#include "cpporm/model_base.h"

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
            std::string column_key;
            std::string extra;
            std::string comment;  // ***** 新增 comment 成员 *****
        };

        struct DbIndexInfo {
            std::string index_name;
            std::vector<std::string> column_names;
            bool is_unique = false;
            bool is_primary_key = false;
            std::string type_method;
        };

        std::string normalizeDbType(const std::string &db_type_raw, const QString &driverNameUpperQ);
        Error migrateCreateTable(Session &session, const ModelMeta &meta, const QString &driverNameUpper);
        std::map<std::string, DbColumnInfo> getTableColumnsInfo(Session &session, const QString &tableNameQString, const QString &driverNameUpper);
        Error migrateModifyColumns(Session &session, const ModelMeta &meta, const QString &driverNameUpper);
        std::map<std::string, DbIndexInfo> getTableIndexesInfo(Session &session, const QString &tableNameQString, const QString &driverNameUpper);
        Error migrateManageIndexes(Session &session, const ModelMeta &meta, const QString &driverNameUpper);
        bool areIndexDefinitionsEquivalent(const DbIndexInfo &db_idx, const IndexDefinition &model_idx_def, const QString &driverNameUpper);
        std::pair<cpporm_sqldriver::SqlQuery, Error> execute_ddl_query(cpporm_sqldriver::SqlDatabase &db, const std::string &ddl_sql_std);

    }  // namespace internal
}  // namespace cpporm

#endif