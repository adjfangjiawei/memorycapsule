// SqlDriver/Include/sqldriver/sql_query.h
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sql_enums.h"  // Provides ParamType, CursorMovement, SqlResultNs enums
#include "sql_error.h"
#include "sql_field.h"  // For SqlField return type
#include "sql_record.h"
#include "sql_value.h"  // Provides NumericalPrecisionPolicy, SqlValue

namespace cpporm_sqldriver {

    class SqlDatabase;  // Forward declaration
    class ISqlDriver;   // Forward declaration
    class SqlResult;    // Forward declaration, SqlQuery uses an instance of this

    class SqlQuery {
      public:
        // Construction
        explicit SqlQuery(SqlDatabase& db);                                           // Create query associated with a database
        explicit SqlQuery(const std::string& query = "", SqlDatabase* db = nullptr);  // Create with optional query and database
        ~SqlQuery();

        // Preparation and Execution
        bool prepare(const std::string& query, SqlResultNs::ScrollMode scroll = SqlResultNs::ScrollMode::ForwardOnly, SqlResultNs::ConcurrencyMode concur = SqlResultNs::ConcurrencyMode::ReadOnly);
        bool exec();                          // Execute a prepared query
        bool exec(const std::string& query);  // Prepare and execute a query

        bool setQueryTimeout(int seconds);  // Sets timeout for query execution

        // Binding Values
        void bindValue(int pos, const SqlValue& val, ParamType type = ParamType::In);
        void bindValue(const std::string& placeholderName, const SqlValue& val, ParamType type = ParamType::In);
        void addBindValue(const SqlValue& val, ParamType type = ParamType::In);  // For batch or simple positional

        // Batch binding (conceptual, depends on how SqlResult handles it)
        // void bindValues(const std::vector<SqlValue>& values, ParamType type = ParamType::In);
        // void bindValues(const std::map<std::string, SqlValue>& values, ParamType type = ParamType::In);

        SqlValue boundValue(int pos) const;
        SqlValue boundValue(const std::string& placeholderName) const;
        // const std::map<std::string, SqlValue>& namedBoundValues() const; // Might expose too much internal state
        // const std::vector<SqlValue>& positionalBoundValues() const;
        void clearBoundValues();
        // int numberOfBoundValues() const;

        // Navigation
        bool next();
        bool previous();                                                           // Requires scrollable cursor
        bool first();                                                              // Requires scrollable cursor
        bool last();                                                               // Requires scrollable cursor
        bool seek(int index, CursorMovement movement = CursorMovement::Absolute);  // Requires scrollable cursor

        // Data Retrieval
        SqlRecord recordMetadata() const;     // Returns metadata of the current result set
        SqlRecord currentFetchedRow() const;  // Returns the currently fetched row as a SqlRecord
        SqlValue value(int index) const;
        SqlValue value(const std::string& name) const;
        bool isNull(int index) const;
        bool isNull(const std::string& name) const;
        SqlField field(int index) const;                // Get field metadata by index
        SqlField field(const std::string& name) const;  // Get field metadata by name

        // Information / State
        int at() const;    // Current 0-based row index in the result set, -1 if not on a valid row
        int size() const;  // Number of rows in the result set (-1 if unknown or not applicable)

        bool isActive() const;  // Is query prepared and/or executed and not finished?
        bool isValid() const;   // Is the result set valid for navigation/data retrieval?
        bool isSelect() const;  // Heuristic: does the query appear to be a SELECT?

        bool setForwardOnly(bool forward);  // Attempt to set result set to forward-only
        bool setNumericalPrecisionPolicy(NumericalPrecisionPolicy policy);
        NumericalPrecisionPolicy numericalPrecisionPolicy() const;

        SqlError lastError() const;
        std::string lastQuery() const;      // The last query string passed to prepare or exec
        std::string executedQuery() const;  // The query text after any placeholder processing by the driver

        // Post-Execution Information
        long long numRowsAffected() const;
        SqlValue lastInsertId() const;  // ID of the last inserted row, if applicable

        // Control
        void finish();  // Releases resources associated with the query and its result. Invalidates the query.
        void clear();   // Synonym for finish()

        // Associated objects
        SqlDatabase* database() const;  // Database this query is associated with
        ISqlDriver* driver() const;     // Underlying driver (use with caution)
        SqlResult* result() const;      // Underlying SqlResult object (use with caution)

        // Batch Execution (conceptual)
        // bool execBatch(BatchExecutionMode mode = BatchExecutionMode::ValuesAsRows);

        // Multiple Result Sets
        bool nextResult();  // Advance to the next result set from a query (e.g., stored procedure)

        // Placeholder syntax
        bool setNamedBindingSyntax(SqlResultNs::NamedBindingSyntax syntax);
        SqlResultNs::NamedBindingSyntax namedBindingSyntax() const;

        // Non-copyable, but movable
        SqlQuery(const SqlQuery&) = delete;
        SqlQuery& operator=(const SqlQuery&) = delete;
        SqlQuery(SqlQuery&& other) noexcept;
        SqlQuery& operator=(SqlQuery&& other) noexcept;

      private:
        // Direct members, no PImpl for simplicity as requested
        SqlDatabase* m_db;                    // Non-owning pointer to the associated database
        std::unique_ptr<SqlResult> m_result;  // The underlying driver-specific result object
        std::string m_last_query_text;
        bool m_is_active;
        bool m_is_select_query;  // Heuristic
        NumericalPrecisionPolicy m_precision_policy;
        SqlResultNs::NamedBindingSyntax m_binding_syntax;

        // Helper to ensure m_result is valid
        bool checkResult(const char* methodName) const;
        void updateSelectStatus();  // Helper to guess if query is SELECT
    };

}  // namespace cpporm_sqldriver