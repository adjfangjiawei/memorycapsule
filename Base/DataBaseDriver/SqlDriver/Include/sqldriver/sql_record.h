// sqldriver/sql_record.h
#pragma once
#include <optional>  // For std::optional
#include <string>
#include <vector>

#include "sql_field.h"  // SqlField 包含元数据和可选的当前值
#include "sql_value.h"

namespace cpporm_sqldriver {

    class SqlRecord {
      public:
        SqlRecord();
        ~SqlRecord();

        // 检查和计数
        bool isEmpty() const;  // 是否包含任何字段
        int count() const;

        // 字段访问
        SqlField field(int index) const;                // 按索引获取字段对象 (包含元数据和值)
        SqlField field(const std::string& name) const;  // 按名称获取字段对象

        std::string fieldName(int index) const;  // 仅获取字段名

        // 值访问 (便捷方法)
        SqlValue value(int index) const;
        SqlValue value(const std::string& name) const;
        bool isNull(int index) const;
        bool isNull(const std::string& name) const;

        // 查找和包含
        int indexOf(const std::string& name) const;  // -1 if not found
        bool contains(const std::string& name) const;

        // 修改 (通常由驱动内部使用来填充记录)
        void append(const SqlField& field);  // 添加一个字段 (元数据+值)
        void insert(int pos, const SqlField& field);
        void remove(int pos);
        void replace(int pos, const SqlField& field);
        void setValue(int index, const SqlValue& val);
        void setValue(const std::string& name, const SqlValue& val);
        void setNull(int index);
        void setNull(const std::string& name);
        void clear();  // 移除所有字段

      private:
        class Private;  // PImpl
        std::unique_ptr<Private> d;

        // SqlRecord 可以被拷贝和赋值
        SqlRecord(const SqlRecord& other);
        SqlRecord& operator=(const SqlRecord& other);
        SqlRecord(SqlRecord&& other) noexcept;
        SqlRecord& operator=(SqlRecord&& other) noexcept;
    };

}  // namespace cpporm_sqldriver