// cpporm_sqldriver/sql_index.h
#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sql_field.h"
#include "sql_value.h"

namespace cpporm_sqldriver {

    enum class IndexSortOrder { Ascending, Descending, Default };
    enum class IndexNullsPosition { Default, First, Last };  // For NULLS FIRST/LAST

    struct IndexColumnDefinition {
        std::string fieldName;
        IndexSortOrder sortOrder = IndexSortOrder::Default;
        IndexNullsPosition nullsPosition = IndexNullsPosition::Default;
        std::optional<std::string> opClass;  // For PostgreSQL operator class
        // std::optional<std::string> collation; // Per-column collation if supported
    };

    class SqlIndex {
      public:
        SqlIndex(const std::string& cursorName = "", const std::string& name = "");
        SqlIndex(const SqlIndex& other);
        SqlIndex& operator=(const SqlIndex& other);
        SqlIndex(SqlIndex&& other) noexcept;
        SqlIndex& operator=(SqlIndex&& other) noexcept;
        ~SqlIndex();

        std::string name() const;
        void setName(const std::string& name);

        std::string cursorName() const;  // Table name
        void setCursorName(const std::string& name);

        std::string schemaName() const;
        void setSchemaName(const std::string& schema);

        bool isUnique() const;
        void setUnique(bool unique);

        bool isPrimaryKey() const;
        void setPrimaryKey(bool pk);

        bool isFunctional() const;  // 是否为函数/表达式索引
        void setFunctional(bool functional);

        void append(const IndexColumnDefinition& colDef);
        void append(const std::string& fieldName, IndexSortOrder order = IndexSortOrder::Default, IndexNullsPosition nulls = IndexNullsPosition::Default, const std::optional<std::string>& opClass = std::nullopt);

        int count() const;
        IndexColumnDefinition columnDefinition(int i) const;

        std::string typeMethod() const;  // e.g., BTREE, HASH, GIN, GIST, SPGIST, BRIN
        void setTypeMethod(const std::string& method);

        std::string condition() const;  // Partial index condition (WHERE clause)
        void setCondition(const std::string& cond);

        std::vector<std::string> includedColumnNames() const;  // SQL Server INCLUDE, PG INCLUDE
        void addIncludedColumn(const std::string& columnName);

        std::map<std::string, SqlValue> options() const;  // e.g., FILLFACTOR, WITH (...)
        void setOption(const std::string& optionName, const SqlValue& value);
        SqlValue option(const std::string& optionName) const;

        void clear();

      private:
        class Private;
        std::unique_ptr<Private> d;
    };

}  // namespace cpporm_sqldriver