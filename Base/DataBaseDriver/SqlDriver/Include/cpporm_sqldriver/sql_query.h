// cpporm_sqldriver/sql_query.h
#pragma once
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sql_driver.h"  // Provides ISqlDriver, SqlResult, ParamType, CursorMovement
#include "sql_error.h"
#include "sql_record.h"
#include "sql_value.h"  // Provides NumericalPrecisionPolicy

namespace cpporm_sqldriver {

    class SqlDatabase;
    class SqlFieldExtendedInfo;  // Defined in sql_field.h, include it if not already by sql_driver.h chain
    // #include "sql_field.h" // Or include directly if SqlFieldExtendedInfo is needed here and not pulled by sql_driver.h

    // ParamType, CursorMovement, NumericalPrecisionPolicy are now sourced from sql_driver.h or sql_value.h

    enum class BatchExecutionMode {
        ValuesAsRows,
        // ValuesAsColumns
    };

    class SqlQuery {
      public:
        explicit SqlQuery(SqlDatabase& db);
        explicit SqlQuery(const std::string& query, SqlDatabase& db);
        ~SqlQuery();

        bool prepare(const std::string& query, SqlResult::ScrollMode scroll = SqlResult::ScrollMode::ForwardOnly, SqlResult::ConcurrencyMode concur = SqlResult::ConcurrencyMode::ReadOnly);
        bool exec();
        bool exec(const std::string& query);
        bool setQueryTimeout(int seconds);
        // bool cancel();

        void bindValue(int pos, const SqlValue& val, ParamType type = ParamType::In);
        void bindValue(const std::string& placeholderName, const SqlValue& val, ParamType type = ParamType::In);
        void addBindValue(const SqlValue& val, ParamType type = ParamType::In);
        void bindValues(const std::vector<SqlValue>& values, ParamType type = ParamType::In);
        void bindValues(const std::map<std::string, SqlValue>& values, ParamType type = ParamType::In);

        SqlValue boundValue(int pos) const;
        SqlValue boundValue(const std::string& placeholderName) const;
        const std::map<std::string, SqlValue>& namedBoundValues() const;
        const std::vector<SqlValue>& positionalBoundValues() const;
        void clearBoundValues();

        bool next();
        bool previous();
        bool first();
        bool last();
        bool seek(int index, CursorMovement movement = CursorMovement::Absolute);

        SqlRecord recordMetadata() const;
        SqlRecord currentFetchedRow() const;
        SqlValue value(int index) const;
        SqlValue value(const std::string& name) const;
        bool isNull(int index) const;
        bool isNull(const std::string& name) const;
        // SqlFieldExtendedInfo fieldExtendedInfo(int index) const; // Needs SqlFieldExtendedInfo definition to be visible
        // SqlFieldExtendedInfo fieldExtendedInfo(const std::string& name) const;

        int at() const;
        int size() const;

        bool isActive() const;
        bool isValid() const;
        bool isSelect() const;
        bool setForwardOnly(bool forward);
        bool setNumericalPrecisionPolicy(NumericalPrecisionPolicy policy);
        SqlError lastError() const;
        std::string lastQuery() const;

        long long numRowsAffected() const;
        SqlValue lastInsertId() const;

        void finish();
        void clear();

        SqlDatabase& database() const;
        ISqlDriver* driver() const;
        SqlResult* result() const;

        bool execBatch(BatchExecutionMode mode = BatchExecutionMode::ValuesAsRows);

        bool nextResult();

      private:
        class Private;
        std::unique_ptr<Private> d;

        SqlQuery(const SqlQuery&) = delete;
        SqlQuery& operator=(const SqlQuery&) = delete;
        SqlQuery(SqlQuery&&) noexcept;
        SqlQuery& operator=(SqlQuery&&) noexcept;
    };

}  // namespace cpporm_sqldriver