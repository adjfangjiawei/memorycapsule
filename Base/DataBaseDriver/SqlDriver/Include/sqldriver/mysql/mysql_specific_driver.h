// SqlDriver/Include/sqldriver/mysql/mysql_specific_driver.h
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_connection_parameters.h"
#include "sqldriver/sql_error.h"

namespace cpporm_mysql_transport {
    class MySqlTransportConnection;
    class MySqlTransportMetadata;
}  // namespace cpporm_mysql_transport

namespace cpporm_sqldriver {

    class MySqlSpecificResult;

    class MySqlSpecificDriver : public ISqlDriver {
      public:
        MySqlSpecificDriver();
        ~MySqlSpecificDriver() override;

        // --- ISqlDriver 接口实现 ---
        bool open(const ConnectionParameters& params) override;
        void close() override;
        bool isOpen() const override;
        bool isOpenError() const override;
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
        // ***** 新增: escapeString 声明 *****
        std::string escapeString(const std::string& unescaped_string) override;
        std::string sqlStatement(StatementType type, const std::string& tableName, const SqlRecord& rec, bool prepared, const std::string& schema = "") const override;

        bool setClientCharset(const std::string& charsetName) override;
        std::string clientCharset() const override;

        SqlValue nextSequenceValue(const std::string& sequenceName, const std::string& schema = "") override;

        SqlValue nativeHandle() const override;
        // --- End ISqlDriver 接口实现 ---

        cpporm_mysql_transport::MySqlTransportConnection* getTransportConnection() const;

      private:
        std::unique_ptr<cpporm_mysql_transport::MySqlTransportConnection> m_transport_connection;
        std::unique_ptr<cpporm_mysql_transport::MySqlTransportMetadata> m_transport_metadata;

        mutable SqlError m_last_error_cache;
        ConnectionParameters m_current_params_cache;
        bool m_open_error_flag;

        void updateLastErrorCacheFromTransport(bool success_of_operation) const;
        std::string resolveSchemaName(const std::string& schemaFilterFromArgs) const;
    };

    void MySqlDriver_Initialize();

}  // namespace cpporm_sqldriver