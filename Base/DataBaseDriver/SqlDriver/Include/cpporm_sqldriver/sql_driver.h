// cpporm_sqldriver/sql_driver.h
#pragma once

#include <any>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sql_error.h"
#include "sql_index.h"
#include "sql_record.h"
#include "sql_value.h"  // Provides NumericalPrecisionPolicy, SqlValue, SqlValueType

namespace cpporm_sqldriver {

    class SqlResult;
    class SqlField;  // Forward declare SqlField for formatValue hint

    enum class Feature {
        Transactions,
        QuerySize,
        BLOB,
        Unicode,
        PreparedQueries,
        NamedPlaceholders,
        PositionalPlaceholders,
        LastInsertId,
        BatchOperations,
        SimpleScrollOnError,
        EventNotifications,
        FinishQuery,
        MultipleResultSets,
        LowPrecisionNumbers,
        CancelQuery,
        InsertAndReturnId,
        NamedSavepoints,
        ThreadSafe,
        SchemaOperations,
        SequenceOperations,
        UpdatableCursors,
        TransactionIsolationLevel,
        GetTypeInfo,
        PingConnection,
        SetQueryTimeout,
        StreamBlob
    };

    enum class IdentifierType { Table, Field, Index, Schema, Sequence, Trigger, View, Constraint, User, Role };
    enum class StatementType { Select, Insert, Update, Delete, DDL, DCL, TCL, Call, Unknown };
    enum class TransactionIsolationLevel { ReadUncommitted, ReadCommitted, RepeatableRead, Serializable, Snapshot, Default };

    // ParamType and CursorMovement definitions are here as SqlResult uses them.
    enum class ParamType { In, Out, InOut, Binary };
    enum class CursorMovement { Absolute, RelativeFirst, RelativeNext, RelativePrevious, RelativeLast };
    // NumericalPrecisionPolicy is now defined in sql_value.h

    // ConnectionParameters struct definition
    struct ConnectionParameters : public std::map<std::string, SqlValue> {
        static const std::string KEY_DB_NAME;
        static const std::string KEY_USER_NAME;
        static const std::string KEY_PASSWORD;
        static const std::string KEY_HOST_NAME;
        static const std::string KEY_PORT;
        static const std::string KEY_CONNECT_OPTIONS;
        static const std::string KEY_CLIENT_CHARSET;
        static const std::string KEY_SSL_MODE;
        static const std::string KEY_SSL_CERT;
        static const std::string KEY_SSL_KEY;
        static const std::string KEY_SSL_CA;
        // ...

        // Helper methods
        void setDbName(const std::string& v) {
            (*this)[KEY_DB_NAME] = v;
        }
        std::optional<std::string> dbName() const {
            auto it = find(KEY_DB_NAME);
            if (it != end() && !it->second.isNull()) {
                bool ok;
                std::string s = it->second.toString(&ok);
                if (ok) return s;
            }
            return std::nullopt;
        }
        // ... other setters/getters for predefined keys can be added ...
        void setUserName(const std::string& v) {
            (*this)[KEY_USER_NAME] = v;
        }
        void setPassword(const std::string& v) {
            (*this)[KEY_PASSWORD] = v;
        }
        void setHostName(const std::string& v) {
            (*this)[KEY_HOST_NAME] = v;
        }
        void setPort(int v) {
            (*this)[KEY_PORT] = v;
        }
        void setConnectOptions(const std::string& v) {
            (*this)[KEY_CONNECT_OPTIONS] = v;
        }
        void setClientCharset(const std::string& v) {
            (*this)[KEY_CLIENT_CHARSET] = v;
        }
    };

    class ISqlDriver {
      public:
        virtual ~ISqlDriver() = default;

        virtual bool open(const ConnectionParameters& params) = 0;
        virtual void close() = 0;
        virtual bool isOpen() const = 0;
        virtual bool isOpenError() const = 0;
        virtual bool ping() = 0;

        virtual bool beginTransaction() = 0;
        virtual bool commitTransaction() = 0;
        virtual bool rollbackTransaction() = 0;
        virtual bool setTransactionIsolationLevel(TransactionIsolationLevel level) = 0;
        virtual TransactionIsolationLevel transactionIsolationLevel() const = 0;
        virtual bool setSavepoint(const std::string& name) = 0;
        virtual bool rollbackToSavepoint(const std::string& name) = 0;
        virtual bool releaseSavepoint(const std::string& name) = 0;

        virtual std::unique_ptr<SqlResult> createResult() const = 0;

        enum class TableType { All, Tables, Views, SystemTables, Aliases, Synonyms };
        virtual std::vector<std::string> tables(TableType type = TableType::Tables, const std::string& schemaFilter = "", const std::string& tableNameFilter = "") const = 0;
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

        virtual SqlValue nativeHandle() const = 0;  // Returns driver-specific native handle (e.g., MYSQL*) wrapped in SqlValue (often as std::any)
    };

    // SqlResult
    class SqlResult {
      public:
        virtual ~SqlResult() = default;

        enum class ScrollMode { ForwardOnly, Scrollable };
        enum class ConcurrencyMode { ReadOnly, Updatable };

        virtual bool prepare(const std::string& query, const std::map<std::string, SqlValue>* named_bindings_meta = nullptr, ScrollMode scroll = ScrollMode::ForwardOnly, ConcurrencyMode concur = ConcurrencyMode::ReadOnly) = 0;
        virtual bool exec() = 0;
        virtual bool setQueryTimeout(int seconds) = 0;
        virtual bool setNumericalPrecisionPolicy(NumericalPrecisionPolicy policy) = 0;  // Uses policy from sql_value.h

        virtual void addPositionalBindValue(const SqlValue& value, ParamType type = ParamType::In) = 0;
        virtual void setNamedBindValue(const std::string& placeholder, const SqlValue& value, ParamType type = ParamType::In) = 0;
        virtual void clearBindValues() = 0;
        virtual void reset() = 0;  // Resets the result for re-execution of prepared query with new bindings
        virtual bool setForwardOnly(bool forward) = 0;

        virtual bool fetchNext(SqlRecord& record_buffer) = 0;
        virtual bool fetchPrevious(SqlRecord& record_buffer) = 0;
        virtual bool fetchFirst(SqlRecord& record_buffer) = 0;
        virtual bool fetchLast(SqlRecord& record_buffer) = 0;
        virtual bool fetch(int index, SqlRecord& record_buffer, CursorMovement movement = CursorMovement::Absolute) = 0;

        virtual SqlValue data(int column_index) = 0;
        virtual bool isNull(int column_index) = 0;
        virtual SqlRecord recordMetadata() const = 0;     // Only column metadata
        virtual SqlRecord currentFetchedRow() const = 0;  // Metadata + data for current row
        // virtual SqlFieldExtendedInfo fieldExtendedInfo(int column_index) const = 0; // Needs SqlFieldExtendedInfo definition

        virtual long long numRowsAffected() = 0;
        virtual SqlValue lastInsertId() = 0;
        virtual int columnCount() const = 0;
        virtual int size() = 0;
        virtual int at() const = 0;

        virtual bool isActive() const = 0;
        virtual bool isValid() const = 0;
        virtual SqlError error() const = 0;
        virtual const std::string& lastQuery() const = 0;

        virtual void finish() = 0;
        virtual void clear() = 0;

        virtual bool nextResult() = 0;
    };
}  // namespace cpporm_sqldriver