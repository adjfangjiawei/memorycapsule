// SqlDriver/Include/sqldriver/sql_database.h
#pragma once
#include <any>
#include <map>
#include <memory>  // Changed to std::shared_ptr for m_driver
#include <string>
#include <vector>

#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_connection_parameters.h"
#include "sqldriver/sql_enums.h"
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_index.h"
#include "sqldriver/sql_record.h"
#include "sqldriver/sql_value.h"

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

        bool transaction();
        bool commit();
        bool rollback();
        bool isTransactionActive() const;  // Checks underlying driver's transaction state
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
        std::string password() const;
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

        ISqlDriver* driver() const;  // Returns raw pointer, lifetime managed by shared_ptr
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

        // Copy constructor and assignment for shared ownership
        SqlDatabase(const SqlDatabase& other);
        SqlDatabase& operator=(const SqlDatabase& other);

        // Move constructor and assignment
        SqlDatabase(SqlDatabase&&) noexcept;
        SqlDatabase& operator=(SqlDatabase&&) noexcept;

      private:
        friend class SqlDriverManager;
        friend class SqlQuery;

        // Private constructor for SqlDriverManager
        SqlDatabase(const std::string& driverTypeFromManager, const std::string& assignedConnectionName,
                    std::shared_ptr<ISqlDriver> driverImplementation);  // Changed to shared_ptr

        std::string m_driver_type_name;
        std::string m_connection_name;
        std::shared_ptr<ISqlDriver> m_driver;  // Changed to shared_ptr
        ConnectionParameters m_parameters;
        mutable SqlError m_last_error;
        // m_transaction_active is removed from SqlDatabase. State is now queried from ISqlDriver.

        void updateLastErrorFromDriver() const;
    };

}  // namespace cpporm_sqldriver