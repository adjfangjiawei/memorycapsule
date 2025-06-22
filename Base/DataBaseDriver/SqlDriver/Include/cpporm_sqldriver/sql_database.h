// cpporm_sqldriver/sql_database.h
#pragma once
#include <any>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "i_sql_driver.h"               // Provides ISqlDriver interface
#include "sql_connection_parameters.h"  // Provides ConnectionParameters
#include "sql_enums.h"                  // Provides Feature, TransactionIsolationLevel, ISqlDriverNs::TableType etc.
#include "sql_error.h"
#include "sql_index.h"   // Provides SqlIndex
#include "sql_record.h"  // Provides SqlRecord

namespace cpporm_sqldriver {

    class SqlQuery;

    class SqlDatabase {
      public:
        ~SqlDatabase();

        bool open(const ConnectionParameters& params);
        bool open();
        bool open(const std::string& user, const std::string& password);
        void close();
        bool isOpen() const;
        bool isValid() const;
        bool ping(int timeout_seconds = 2);

        bool transaction();  // Renamed from beginTransaction as per error log implication
        bool commit();
        bool rollback();
        bool isTransactionActive() const;
        bool setTransactionIsolationLevel(TransactionIsolationLevel level);
        TransactionIsolationLevel transactionIsolationLevel() const;
        bool setSavepoint(const std::string& name);
        bool rollbackToSavepoint(const std::string& name);
        bool releaseSavepoint(const std::string& name);

        std::string driverName() const;
        std::string databaseName() const;
        void setDatabaseName(const std::string& name);

        std::string userName() const;
        void setUserName(const std::string& name);

        void setPassword(const std::string& password);

        std::string hostName() const;
        void setHostName(const std::string& host);

        int port() const;
        void setPort(int port);

        std::string connectOptionsString() const;
        void setConnectOptionsString(const std::string& options);

        const ConnectionParameters& connectionParameters() const;
        void setConnectionParameter(const std::string& key, const SqlValue& value);
        SqlValue connectionParameter(const std::string& key) const;

        SqlError lastError() const;

        ISqlDriver* driver() const;
        std::string connectionName() const;

        std::vector<std::string> tables(ISqlDriverNs::TableType type = ISqlDriverNs::TableType::Tables, const std::string& schemaFilter = "", const std::string& tableNameFilter = "") const;
        std::vector<std::string> schemas(const std::string& schemaFilter = "") const;
        SqlRecord record(const std::string& tablename, const std::string& schema = "") const;
        SqlIndex primaryIndex(const std::string& tablename, const std::string& schema = "") const;
        std::vector<SqlIndex> indexes(const std::string& tablename, const std::string& schema = "") const;

        bool hasFeature(Feature feature) const;
        SqlValue nativeHandle() const;
        std::string databaseProductVersion() const;
        std::string driverVersion() const;

        bool setClientCharset(const std::string& charsetName);
        std::string clientCharset() const;

        SqlValue nextSequenceValue(const std::string& sequenceName, const std::string& schema = "");

        // Move operations made public
        SqlDatabase(SqlDatabase&&) noexcept;
        SqlDatabase& operator=(SqlDatabase&&) noexcept;

      private:
        friend class SqlDriverManager;
        friend class SqlQuery;

        SqlDatabase(const std::string& driverTypeFromManager, const std::string& assignedConnectionName, std::unique_ptr<ISqlDriver> driverImplementation);

        class Private;
        std::unique_ptr<Private> d;

        SqlDatabase(const SqlDatabase&) = delete;
        SqlDatabase& operator=(const SqlDatabase&) = delete;
        // Moved constructors/assignment are now public
    };

}  // namespace cpporm_sqldriver