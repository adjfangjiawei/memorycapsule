// cpporm_sqldriver/sql_index.h
#pragma once
#include <map>  // For index options
#include <string>
#include <vector>

#include "sql_field.h"
#include "sql_value.h"  // For index options if they have values

namespace cpporm_sqldriver {

    struct IndexColumnDefinition {
        std::string fieldName;
        bool isDescending = false;
        // std::string opClass; // For PostgreSQL operator class
        // NullsSortOrder nullsOrder; // For NULLS FIRST/LAST
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

        std::string cursorName() const;
        void setCursorName(const std::string& name);

        bool isUnique() const;
        void setUnique(bool unique);

        bool isPrimaryKey() const;
        void setPrimaryKey(bool pk);

        // 字段列表 (现在使用 IndexColumnDefinition)
        void append(const IndexColumnDefinition& colDef);
        void append(const std::string& fieldName, bool isDescending = false);

        int count() const;
        IndexColumnDefinition columnDefinition(int i) const;  // 获取字段定义
        // SqlField field(int i) const; // 可能仍然有用，返回一个SqlField元数据对象
        // bool isDescending(int i) const; // 已移至 IndexColumnDefinition
        // void setDescending(int i, bool descend); // 已移至 IndexColumnDefinition

        std::string typeMethod() const;  // e.g., BTREE, HASH, GIN, GIST
        void setTypeMethod(const std::string& method);

        std::string condition() const;  // Partial index condition (WHERE clause)
        void setCondition(const std::string& cond);

        // 包含列 (SQL Server, PostgreSQL 11+)
        std::vector<std::string> includedColumnNames() const;
        void addIncludedColumn(const std::string& columnName);

        // 驱动/数据库特定的索引选项 (e.g., FILLFACTOR, CONCURRENTLY)
        std::map<std::string, SqlValue> options() const;
        void setOption(const std::string& optionName, const SqlValue& value);

        void clear();

      private:
        class Private;
        std::unique_ptr<Private> d;
    };

}  // namespace cpporm_sqldriver