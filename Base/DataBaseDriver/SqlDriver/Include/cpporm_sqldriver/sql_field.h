// cpporm_sqldriver/sql_field.h
#pragma once
#include <any>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sql_value.h"

namespace cpporm_sqldriver {

    enum class RequiredStatus { Unknown = -1, Optional = 0, Required = 1 };

    class SqlField {
      public:
        SqlField(const std::string& name = "", SqlValueType type = SqlValueType::Null, const std::string& db_type_name = "");
        SqlField(const SqlField& other);
        SqlField& operator=(const SqlField& other);
        SqlField(SqlField&& other) noexcept;
        SqlField& operator=(SqlField&& other) noexcept;
        ~SqlField();

        std::string name() const;
        void setName(const std::string& name);

        SqlValueType type() const;
        void setType(SqlValueType type);

        std::string databaseTypeName() const;
        void setDatabaseTypeName(const std::string& name);
        int driverType() const;
        void setDriverType(int typeId);

        int length() const;
        void setLength(int len);

        int precision() const;
        void setPrecision(int prec);

        int scale() const;
        void setScale(int s);

        bool isNullInValue() const;
        bool isAutoValue() const;
        void setAutoValue(bool autoVal);

        bool isReadOnly() const;
        void setReadOnly(bool ro);

        RequiredStatus requiredStatus() const;
        void setRequiredStatus(RequiredStatus status);

        SqlValue defaultValue() const;
        void setDefaultValue(const SqlValue& value);

        SqlValue value() const;
        void setValue(const SqlValue& value);
        void clearValue();

        bool isValid() const;
        bool isGenerated() const;
        void setGenerated(bool generated);

        bool isPrimaryKeyPart() const;
        void setPrimaryKeyPart(bool is_pk);

        bool isForeignKeyPart() const;
        void setForeignKeyPart(bool is_fk);
        std::optional<std::string> referencedTableName() const;
        void setReferencedTableName(const std::optional<std::string>& name);
        std::optional<std::string> referencedColumnName() const;
        void setReferencedColumnName(const std::optional<std::string>& name);

        std::optional<std::string> collationName() const;
        void setCollationName(const std::optional<std::string>& name);

        bool isExpression() const;
        void setIsExpression(bool is_expr);
        std::optional<std::string> aliasName() const;
        void setAliasName(const std::optional<std::string>& alias);
        std::optional<std::string> baseTableName() const;
        void setBaseTableName(const std::optional<std::string>& name);
        std::optional<std::string> baseColumnName() const;
        void setBaseColumnName(const std::optional<std::string>& name);
        std::optional<std::string> baseSchemaName() const;
        void setBaseSchemaName(const std::optional<std::string>& name);

        std::any metaData() const;
        void setMetaData(const std::any& data);

      private:
        class Private;
        std::unique_ptr<Private> d;
    };

}  // namespace cpporm_sqldriver