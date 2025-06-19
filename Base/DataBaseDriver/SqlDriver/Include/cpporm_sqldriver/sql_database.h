// cpporm_sqldriver/sql_database.h
#pragma once
#include <any>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sql_driver.h"  // Provides ConnectionParameters, Feature, ISqlDriver, TableType, TransactionIsolationLevel
#include "sql_error.h"
#include "sql_index.h"   // Provides SqlIndex
#include "sql_record.h"  // Provides SqlRecord
// SqlQuery is forward declared below as it needs SqlDatabase in its constructor

namespace cpporm_sqldriver {

    class SqlQuery;  // Forward declare to break potential cycle with sql_query.h

    class SqlDatabase {
      public:
        ~SqlDatabase();

        // 使用 ConnectionParameters 打开连接
        bool open(const ConnectionParameters& params);
        // 保留旧的 open 接口作为便捷方法
        bool open();
        bool open(const std::string& user, const std::string& password);
        void close();
        bool isOpen() const;
        bool isValid() const;
        bool ping();

        bool transaction();
        bool commit();
        bool rollback();
        bool isTransactionActive() const;
        bool setTransactionIsolationLevel(TransactionIsolationLevel level);
        TransactionIsolationLevel transactionIsolationLevel() const;
        bool setSavepoint(const std::string& name);
        bool rollbackToSavepoint(const std::string& name);
        bool releaseSavepoint(const std::string& name);

        std::string driverName() const;                 // Registered driver type name
        std::string databaseName() const;               // Name from ConnectionParameters
        void setDatabaseName(const std::string& name);  // Sets param for next open

        std::string userName() const;
        void setUserName(const std::string& name);  // Sets param for next open

        void setPassword(const std::string& password);  // Sets param for next open

        std::string hostName() const;
        void setHostName(const std::string& host);  // Sets param for next open

        int port() const;
        void setPort(int port);  // Sets param for next open

        std::string connectOptions() const;
        void setConnectOptions(const std::string& options);        // Sets param for next open
        const ConnectionParameters& connectionParameters() const;  // Get all current parameters

        SqlError lastError() const;

        ISqlDriver* driver() const;
        std::string connectionName() const;

        // 元数据查询
        std::vector<std::string> tables(ISqlDriver::TableType type = ISqlDriver::TableType::Tables, const std::string& schemaFilter = "", const std::string& tableNameFilter = "") const;
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
        // bool createSchema(const std::string& schemaName);
        // bool dropSchema(const std::string& schemaName);

      private:
        friend class SqlDriverManager;
        friend class SqlQuery;

        // 私有构造函数，只能由 SqlDriverManager 创建
        SqlDatabase(const std::string& driverTypeFromManager, const std::string& assignedConnectionName, std::unique_ptr<ISqlDriver> driverImplementation);

        class Private;
        std::unique_ptr<Private> d;

        // 禁止拷贝
        SqlDatabase(const SqlDatabase&) = delete;
        SqlDatabase& operator=(const SqlDatabase&) = delete;
        // 允许移动
        SqlDatabase(SqlDatabase&&) noexcept;
        SqlDatabase& operator=(SqlDatabase&&) noexcept;
    };

}  // namespace cpporm_sqldriver