// SqlDriver/Include/sqldriver/sql_index.h
#pragma once
#include <map>
#include <memory>  // For std::unique_ptr if PImpl were used
#include <optional>
#include <string>
#include <vector>

#include "sqldriver/sql_value.h"  // For SqlValue in options
// SqlField is not directly part of SqlIndex definition usually, but IndexColumnDefinition uses fieldName.

namespace cpporm_sqldriver {

    enum class IndexSortOrder {
        Default,  // Let the database decide (usually Ascending)
        Ascending,
        Descending
    };

    enum class IndexNullsPosition {
        Default,  // Database default (e.g., NULLS LAST for ASC, NULLS FIRST for DESC in PG)
        First,    // NULLS FIRST
        Last      // NULLS LAST
    };

    struct IndexColumnDefinition {
        std::string fieldName;  // Name of the column in the index
        IndexSortOrder sortOrder = IndexSortOrder::Default;
        IndexNullsPosition nullsPosition = IndexNullsPosition::Default;  // If DB supports NULLS FIRST/LAST
        std::optional<std::string> expression;                           // For functional indexes, this holds the expression if fieldName isn't sufficient
        std::optional<std::string> opClass;                              // For DBs like PostgreSQL (e.g., "text_pattern_ops")
        // std::optional<std::string> collation; // Per-column collation in index (less common)
        // std::optional<int> subPartLength; // For prefix indexing (e.g. MySQL VARCHAR(255) a_column(10))
    };

    class SqlIndex {
      public:
        SqlIndex(const std::string& name = "", const std::string& tableName = "", const std::string& schemaName = "");
        ~SqlIndex();

        // Copy and Move semantics
        SqlIndex(const SqlIndex& other);
        SqlIndex& operator=(const SqlIndex& other);
        SqlIndex(SqlIndex&& other) noexcept;
        SqlIndex& operator=(SqlIndex&& other) noexcept;

        // Basic Properties
        std::string name() const;  // Index name
        void setName(const std::string& name);

        std::string tableName() const;  // Table this index belongs to (renamed from cursorName for clarity)
        void setTableName(const std::string& name);

        std::string schemaName() const;  // Schema of the table
        void setSchemaName(const std::string& schema);

        // Index Characteristics
        bool isUnique() const;
        void setUnique(bool unique);

        bool isPrimaryKey() const;
        void setPrimaryKey(bool pk);

        bool isFunctional() const;  // True if index is on an expression rather than just columns
        void setFunctional(bool functional);

        std::string typeMethod() const;  // Index method (e.g., "BTREE", "HASH", "GIN", "SPATIAL")
        void setTypeMethod(const std::string& method);

        // Columns in the Index
        void appendColumn(const IndexColumnDefinition& colDef);
        void appendColumn(const std::string& fieldName, IndexSortOrder order = IndexSortOrder::Default, const std::optional<std::string>& expression = std::nullopt, IndexNullsPosition nulls = IndexNullsPosition::Default, const std::optional<std::string>& opClass = std::nullopt);

        int columnCount() const;
        IndexColumnDefinition column(int i) const;                  // Get definition of i-th column in index
        const std::vector<IndexColumnDefinition>& columns() const;  // Get all column definitions

        // Advanced Index Properties
        std::string condition() const;  // Partial index condition (WHERE clause for CREATE INDEX)
        void setCondition(const std::string& cond);

        std::vector<std::string> includedColumnNames() const;  // For covering indexes (e.g., SQL Server INCLUDE)
        void addIncludedColumn(const std::string& columnName);
        void setIncludedColumns(const std::vector<std::string>& columnNames);

        // Driver/DB specific options
        std::map<std::string, SqlValue> options() const;  // e.g., FILLFACTOR, WITH (...)
        void setOption(const std::string& optionName, const SqlValue& value);
        SqlValue option(const std::string& optionName) const;

        void clear();  // Resets the SqlIndex to a default state

      private:
        // Direct members
        std::string m_name;
        std::string m_table_name;
        std::string m_schema_name;
        bool m_is_unique;
        bool m_is_primary_key;
        bool m_is_functional;
        std::string m_type_method;  // e.g. BTREE
        std::vector<IndexColumnDefinition> m_columns;

        std::string m_condition;                      // For partial indexes
        std::vector<std::string> m_included_columns;  // For covering indexes
        std::map<std::string, SqlValue> m_options;    // Other options like fillfactor etc.
    };

}  // namespace cpporm_sqldriver