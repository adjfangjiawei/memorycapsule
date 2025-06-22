// SqlDriver/Include/sqldriver/sql_database.h
#pragma once
#include <any>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sqldriver/i_sql_driver.h"               // 提供 ISqlDriver 接口
#include "sqldriver/sql_connection_parameters.h"  // 提供 ConnectionParameters
#include "sqldriver/sql_enums.h"                  // 提供 Feature, TransactionIsolationLevel, ISqlDriverNs::TableType 等
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_index.h"   // 提供 SqlIndex
#include "sqldriver/sql_record.h"  // 提供 SqlRecord
#include "sqldriver/sql_value.h"   // 提供 SqlValue

namespace cpporm_sqldriver {

    class SqlQuery;  // 前向声明

    class SqlDatabase {
      public:
        // SqlDatabase 的构造函数由 SqlDriverManager 调用
        // 用户通常不直接构造 SqlDatabase 对象
        ~SqlDatabase();

        // 连接管理
        bool open(const ConnectionParameters& params);
        bool open();                                                      // 使用已存储的参数打开
        bool open(const std::string& user, const std::string& password);  // 使用已存储参数，但覆盖用户和密码
        void close();
        bool isOpen() const;
        bool isValid() const;  // 检查驱动是否已成功加载
        bool ping(int timeout_seconds = 2);

        // 事务管理
        bool transaction();  // 开始事务
        bool commit();
        bool rollback();
        bool isTransactionActive() const;
        bool setTransactionIsolationLevel(TransactionIsolationLevel level);
        TransactionIsolationLevel transactionIsolationLevel() const;
        bool setSavepoint(const std::string& name);
        bool rollbackToSavepoint(const std::string& name);
        bool releaseSavepoint(const std::string& name);

        // 连接属性
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

        // 驱动和元数据访问
        ISqlDriver* driver() const;
        std::string connectionName() const;

        std::vector<std::string> tables(ISqlDriverNs::TableType type = ISqlDriverNs::TableType::Tables, const std::string& schemaFilter = "", const std::string& tableNameFilter = "") const;
        std::vector<std::string> schemas(const std::string& schemaFilter = "") const;
        SqlRecord record(const std::string& tablename, const std::string& schema = "") const;
        SqlIndex primaryIndex(const std::string& tablename, const std::string& schema = "") const;
        std::vector<SqlIndex> indexes(const std::string& tablename, const std::string& schema = "") const;

        // 特性支持和版本信息
        bool hasFeature(Feature feature) const;
        SqlValue nativeHandle() const;
        std::string databaseProductVersion() const;
        std::string driverVersion() const;

        // 字符集
        bool setClientCharset(const std::string& charsetName);
        std::string clientCharset() const;

        // 序列
        SqlValue nextSequenceValue(const std::string& sequenceName, const std::string& schema = "");

        // 移动语义
        SqlDatabase(SqlDatabase&&) noexcept;
        SqlDatabase& operator=(SqlDatabase&&) noexcept;

      private:
        friend class SqlDriverManager;  // SqlDriverManager 可以访问私有构造函数
        friend class SqlQuery;          // SqlQuery 需要访问私有成员或方法

        // 私有构造函数，由 SqlDriverManager 调用
        SqlDatabase(const std::string& driverTypeFromManager, const std::string& assignedConnectionName, std::unique_ptr<ISqlDriver> driverImplementation);

        // 直接声明成员变量
        std::string m_driver_type_name;        // 驱动类型名 (例如 "MYSQL")
        std::string m_connection_name;         // 此 SqlDatabase 实例的连接名
        std::unique_ptr<ISqlDriver> m_driver;  // 底层驱动实例
        ConnectionParameters m_parameters;     // 当前连接参数
        mutable SqlError m_last_error;         // 最近的错误信息 (mutable 允许 const 方法修改)
        bool m_transaction_active;             // 跟踪事务是否激活

        // 禁止拷贝构造和拷贝赋值，因为 SqlDatabase 管理着唯一的驱动实例
        SqlDatabase(const SqlDatabase&) = delete;
        SqlDatabase& operator=(const SqlDatabase&) = delete;

        // 辅助函数，用于从底层驱动更新 lastError
        void updateLastErrorFromDriver() const;  // 声明为 const，因为它修改 mutable 成员
    };

}  // namespace cpporm_sqldriver