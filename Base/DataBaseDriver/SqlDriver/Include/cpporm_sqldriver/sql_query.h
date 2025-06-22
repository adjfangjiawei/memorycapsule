// cpporm_sqldriver/sql_query.h
#pragma once
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "i_sql_driver.h"  // Provides ISqlDriver
#include "sql_enums.h"     // Provides ParamType, CursorMovement, SqlResultNs enums
#include "sql_error.h"
#include "sql_field.h"  // Provides SqlField
#include "sql_record.h"
#include "sql_result.h"  // Provides SqlResult
#include "sql_value.h"   // Provides NumericalPrecisionPolicy

namespace cpporm_sqldriver {

    class SqlDatabase;

    enum class BatchExecutionMode {
        ValuesAsRows,
    };

    class SqlQuery {
      public:
        explicit SqlQuery(SqlDatabase& db);
        explicit SqlQuery(const std::string& query, SqlDatabase& db);
        ~SqlQuery();

        bool prepare(const std::string& query, SqlResultNs::ScrollMode scroll = SqlResultNs::ScrollMode::ForwardOnly, SqlResultNs::ConcurrencyMode concur = SqlResultNs::ConcurrencyMode::ReadOnly);
        bool exec();
        bool exec(const std::string& query);
        bool setQueryTimeout(int seconds);

        void bindValue(int pos, const SqlValue& val, ParamType type = ParamType::In, int size_hint_for_out_param = 0);
        void bindValue(const std::string& placeholderName, const SqlValue& val, ParamType type = ParamType::In, int size_hint_for_out_param = 0);
        void addBindValue(const SqlValue& val, ParamType type = ParamType::In);
        void bindValues(const std::vector<SqlValue>& values, ParamType type = ParamType::In);
        void bindValues(const std::map<std::string, SqlValue>& values, ParamType type = ParamType::In);

        SqlValue boundValue(int pos) const;
        SqlValue boundValue(const std::string& placeholderName) const;
        const std::map<std::string, SqlValue>& namedBoundValues() const;
        const std::vector<SqlValue>& positionalBoundValues() const;
        void clearBoundValues();
        int numberOfBoundValues() const;

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
        SqlField field(int index) const;
        SqlField field(const std::string& name) const;

        int at() const;
        int size() const;

        bool isActive() const;
        bool isValid() const;
        bool isSelect() const;
        bool setForwardOnly(bool forward);
        bool setNumericalPrecisionPolicy(NumericalPrecisionPolicy policy);
        SqlError lastError() const;
        std::string lastQuery() const;
        std::string executedQuery() const;

        long long numRowsAffected() const;
        SqlValue lastInsertId() const;

        void finish();
        void clear();

        SqlDatabase& database() const;
        ISqlDriver* driver() const;
        SqlResult* result() const;

        bool execBatch(BatchExecutionMode mode = BatchExecutionMode::ValuesAsRows);

        bool nextResult();

        // Move operations made public
        SqlQuery(SqlQuery&&) noexcept;
        SqlQuery& operator=(SqlQuery&&) noexcept;

      private:
        class Private;
        std::unique_ptr<Private> d;

        SqlQuery(const SqlQuery&) = delete;
        SqlQuery& operator=(const SqlQuery&) = delete;
        // Moved constructors/assignment are now public
    };

}  // namespace cpporm_sqldriver