// SqlDriver/Include/sqldriver/mysql/mysql_specific_driver.h
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_connection_parameters.h"  // For m_current_params_cache
#include "sqldriver/sql_error.h"                  // For m_last_error_cache

// 前向声明 MySqlTransport 类
namespace cpporm_mysql_transport {
    class MySqlTransportConnection;
    class MySqlTransportMetadata;
}  // namespace cpporm_mysql_transport

namespace cpporm_sqldriver {

    class MySqlSpecificResult;  // 前向声明

    class MySqlSpecificDriver : public ISqlDriver {
      public:
        MySqlSpecificDriver();
        ~MySqlSpecificDriver() override;

        // --- ISqlDriver 接口实现 ---
        bool open(const ConnectionParameters& params) override;
        void close() override;
        bool isOpen() const override;
        bool isOpenError() const override;  // 如果上次 open() 失败或连接意外断开，则为 true
        bool ping(int timeout_seconds = 2) override;

        bool beginTransaction() override;
        bool commitTransaction() override;
        bool rollbackTransaction() override;
        bool setTransactionIsolationLevel(TransactionIsolationLevel level) override;
        TransactionIsolationLevel transactionIsolationLevel() const override;
        bool setSavepoint(const std::string& name) override;
        bool rollbackToSavepoint(const std::string& name) override;
        bool releaseSavepoint(const std::string& name) override;

        std::unique_ptr<SqlResult> createResult() const override;

        std::vector<std::string> tables(ISqlDriverNs::TableType type = ISqlDriverNs::TableType::Tables, const std::string& schemaFilter = "", const std::string& tableNameFilter = "") const override;
        std::vector<std::string> schemas(const std::string& schemaFilter = "") const override;
        SqlRecord record(const std::string& tablename, const std::string& schema = "") const override;
        SqlIndex primaryIndex(const std::string& tablename, const std::string& schema = "") const override;
        std::vector<SqlIndex> indexes(const std::string& tablename, const std::string& schema = "") const override;

        bool hasFeature(Feature feature) const override;
        SqlError lastError() const override;
        std::string databaseProductVersion() const override;
        std::string driverVersion() const override;

        std::string formatValue(const SqlValue& value, SqlValueType type_hint = SqlValueType::Null, const SqlField* field_meta_hint = nullptr) const override;
        std::string escapeIdentifier(const std::string& identifier, IdentifierType type) const override;
        std::string sqlStatement(StatementType type, const std::string& tableName, const SqlRecord& rec, bool prepared, const std::string& schema = "") const override;

        bool setClientCharset(const std::string& charsetName) override;
        std::string clientCharset() const override;

        SqlValue nextSequenceValue(const std::string& sequenceName, const std::string& schema = "") override;

        SqlValue nativeHandle() const override;
        // --- End ISqlDriver 接口实现 ---

        // 供 MySqlSpecificResult 访问 transport connection
        cpporm_mysql_transport::MySqlTransportConnection* getTransportConnection() const;

      private:
        std::unique_ptr<cpporm_mysql_transport::MySqlTransportConnection> m_transport_connection;
        std::unique_ptr<cpporm_mysql_transport::MySqlTransportMetadata> m_transport_metadata;  // 添加 MySqlTransportMetadata 实例

        mutable SqlError m_last_error_cache;          // 存储最近的错误信息，声明为 mutable 以便 const 方法可以更新它
        ConnectionParameters m_current_params_cache;  // 缓存当前连接参数
        bool m_open_error_flag;                       // 标记上次打开操作是否失败

        // 辅助函数，用于从 transport 层更新 m_last_error_cache
        void updateLastErrorCacheFromTransport(bool success_of_operation) const;  // 声明为 const

        // 解析 schema 名称 (如果参数为空，则使用连接参数中的 db_name)
        std::string resolveSchemaName(const std::string& schemaFilterFromArgs) const;
    };

}  // namespace cpporm_sqldriver