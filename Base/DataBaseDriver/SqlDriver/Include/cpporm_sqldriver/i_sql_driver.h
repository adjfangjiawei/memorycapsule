// cpporm_sqldriver/i_sql_driver.h
#pragma once

#include <any>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sql_connection_parameters.h"  // Provides ConnectionParameters
#include "sql_enums.h"                  // Provides Feature, IdentifierType, StatementType, TransactionIsolationLevel, ISqlDriverNs::TableType
#include "sql_error.h"
#include "sql_index.h"
#include "sql_record.h"
#include "sql_value.h"  // For SqlValue, SqlValueType

namespace cpporm_sqldriver {

    class SqlResult;
    class SqlField;

    class ISqlDriver {
      public:
        virtual ~ISqlDriver() = default;

        virtual bool open(const ConnectionParameters& params) = 0;
        virtual void close() = 0;
        virtual bool isOpen() const = 0;
        virtual bool isOpenError() const = 0;
        virtual bool ping(int timeout_seconds = 2) = 0;

        virtual bool beginTransaction() = 0;
        virtual bool commitTransaction() = 0;
        virtual bool rollbackTransaction() = 0;
        virtual bool setTransactionIsolationLevel(TransactionIsolationLevel level) = 0;
        virtual TransactionIsolationLevel transactionIsolationLevel() const = 0;
        virtual bool setSavepoint(const std::string& name) = 0;
        virtual bool rollbackToSavepoint(const std::string& name) = 0;
        virtual bool releaseSavepoint(const std::string& name) = 0;

        virtual std::unique_ptr<SqlResult> createResult() const = 0;

        virtual std::vector<std::string> tables(ISqlDriverNs::TableType type = ISqlDriverNs::TableType::Tables, const std::string& schemaFilter = "", const std::string& tableNameFilter = "") const = 0;
        virtual std::vector<std::string> schemas(const std::string& schemaFilter = "") const = 0;
        virtual SqlRecord record(const std::string& tablename, const std::string& schema = "") const = 0;
        virtual SqlIndex primaryIndex(const std::string& tablename, const std::string& schema = "") const = 0;
        virtual std::vector<SqlIndex> indexes(const std::string& tablename, const std::string& schema = "") const = 0;

        virtual bool hasFeature(Feature feature) const = 0;
        virtual SqlError lastError() const = 0;
        virtual std::string databaseProductVersion() const = 0;
        virtual std::string driverVersion() const = 0;

        virtual std::string formatValue(const SqlValue& value, SqlValueType type_hint = SqlValueType::Null, const SqlField* field_meta_hint = nullptr) const = 0;
        virtual std::string escapeIdentifier(const std::string& identifier, IdentifierType type) const = 0;
        virtual std::string sqlStatement(StatementType type, const std::string& tableName, const SqlRecord& rec, bool prepared, const std::string& schema = "") const = 0;

        virtual bool setClientCharset(const std::string& charsetName) = 0;
        virtual std::string clientCharset() const = 0;

        virtual SqlValue nextSequenceValue(const std::string& sequenceName, const std::string& schema = "") = 0;

        virtual SqlValue nativeHandle() const = 0;
    };

}  // namespace cpporm_sqldriver