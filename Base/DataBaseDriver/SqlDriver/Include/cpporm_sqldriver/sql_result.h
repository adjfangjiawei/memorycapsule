// cpporm_sqldriver/sql_result.h
#pragma once

#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sql_enums.h"  // Provides ParamType, CursorMovement, SqlResultNs enums
#include "sql_error.h"
#include "sql_record.h"
#include "sql_value.h"  // Provides SqlValue, NumericalPrecisionPolicy, SqlValueType
// #include "sql_field.h" // Forward declare or include if needed

namespace cpporm_sqldriver {

    class SqlField;  // Forward declare for SqlResult::field()
    // struct SqlFieldExtendedInfo; // Forward declare if used

    class SqlResult {
      public:
        virtual ~SqlResult() = default;

        virtual bool prepare(const std::string& query, const std::map<std::string, SqlValueType>* named_bindings_type_hints = nullptr, SqlResultNs::ScrollMode scroll = SqlResultNs::ScrollMode::ForwardOnly, SqlResultNs::ConcurrencyMode concur = SqlResultNs::ConcurrencyMode::ReadOnly) = 0;
        virtual bool exec() = 0;
        virtual bool setQueryTimeout(int seconds) = 0;
        virtual bool setNumericalPrecisionPolicy(NumericalPrecisionPolicy policy) = 0;
        virtual bool setPrefetchSize(int rows) = 0;
        virtual int prefetchSize() const = 0;

        virtual void addPositionalBindValue(const SqlValue& value, ParamType type = ParamType::In) = 0;
        virtual void setNamedBindValue(const std::string& placeholder, const SqlValue& value, ParamType type = ParamType::In) = 0;
        virtual void bindBlobStream(int pos, std::shared_ptr<std::istream> stream, long long size = -1, ParamType type = ParamType::In) = 0;
        virtual void bindBlobStream(const std::string& placeholder, std::shared_ptr<std::istream> stream, long long size = -1, ParamType type = ParamType::In) = 0;
        virtual void clearBindValues() = 0;
        virtual void reset() = 0;
        virtual bool setForwardOnly(bool forward) = 0;

        virtual bool fetchNext(SqlRecord& record_buffer) = 0;
        virtual bool fetchPrevious(SqlRecord& record_buffer) = 0;
        virtual bool fetchFirst(SqlRecord& record_buffer) = 0;
        virtual bool fetchLast(SqlRecord& record_buffer) = 0;
        virtual bool fetch(int index, SqlRecord& record_buffer, CursorMovement movement = CursorMovement::Absolute) = 0;

        virtual SqlValue data(int column_index) = 0;
        virtual std::shared_ptr<std::istream> openReadableBlobStream(int column_index) = 0;
        virtual std::shared_ptr<std::ostream> openWritableBlobStream(int column_index, long long initial_size_hint = 0) = 0;

        virtual bool isNull(int column_index) = 0;
        virtual SqlRecord recordMetadata() const = 0;
        virtual SqlRecord currentFetchedRow() const = 0;
        virtual SqlField field(int column_index) const = 0;
        // virtual SqlFieldExtendedInfo fieldExtendedInfo(int column_index) const = 0;

        virtual long long numRowsAffected() = 0;
        virtual SqlValue lastInsertId() = 0;
        virtual int columnCount() const = 0;
        virtual int size() = 0;
        virtual int at() const = 0;

        virtual bool isActive() const = 0;
        virtual bool isValid() const = 0;
        virtual SqlError error() const = 0;
        virtual const std::string& lastQuery() const = 0;
        virtual const std::string& preparedQueryText() const = 0;

        virtual void finish() = 0;
        virtual void clear() = 0;

        virtual bool nextResult() = 0;

        virtual SqlValue getOutParameter(int pos) const = 0;
        virtual SqlValue getOutParameter(const std::string& name) const = 0;
        virtual std::map<std::string, SqlValue> getAllOutParameters() const = 0;

        virtual bool setNamedBindingSyntax(SqlResultNs::NamedBindingSyntax syntax) = 0;
    };

}  // namespace cpporm_sqldriver