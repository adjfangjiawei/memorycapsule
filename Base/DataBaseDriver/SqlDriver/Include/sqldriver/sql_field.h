// SqlDriver/Include/sqldriver/sql_field.h
#pragma once
#include <any>
#include <memory>  // For std::unique_ptr if PImpl were used
#include <optional>
#include <string>
#include <vector>

#include "sqldriver/sql_value.h"  // Contains SqlValueType and SqlValue

namespace cpporm_sqldriver {

    // RequiredStatus indicates if a field must have a value.
    // Based on database's NOT NULL constraint.
    enum class RequiredStatus {
        Unknown = -1,  // Status cannot be determined
        Optional = 0,  // Field can be NULL (is nullable)
        Required = 1   // Field must not be NULL (is NOT NULL)
    };

    class SqlField {
      public:
        // Constructors
        SqlField(const std::string& name = "", SqlValueType type = SqlValueType::Null, const std::string& db_type_name = "");
        ~SqlField();

        // Copy and Move semantics
        SqlField(const SqlField& other);
        SqlField& operator=(const SqlField& other);
        SqlField(SqlField&& other) noexcept;
        SqlField& operator=(SqlField&& other) noexcept;

        // Basic Properties
        std::string name() const;
        void setName(const std::string& name);

        SqlValue value() const;  // Current value of the field
        void setValue(const SqlValue& value);
        void clearValue();           // Sets the value to null
        bool isNullInValue() const;  // Checks if the current value is null

        // Type Information
        SqlValueType type() const;  // Generic CppOrm SqlValueType
        void setType(SqlValueType type);

        std::string databaseTypeName() const;  // Native database type name (e.g., "VARCHAR(255)")
        void setDatabaseTypeName(const std::string& name);

        int driverType() const;          // Driver-specific type ID (e.g., from an enum like MYSQL_TYPE_*)
        void setDriverType(int typeId);  // Set driver-specific type ID

        // Size and Precision
        int length() const;  // For strings: max chars; For numerics: display width or total digits
        void setLength(int len);

        int precision() const;  // For numerics: total number of digits (excluding sign/decimal point for some DBs)
                                // For time/timestamp: fractional seconds precision
        void setPrecision(int prec);

        int scale() const;     // For numerics: number of digits after the decimal point
        void setScale(int s);  // For numerics like DECIMAL(P,S)

        // Constraints and Attributes
        RequiredStatus requiredStatus() const;  // Is the field NOT NULL?
        void setRequiredStatus(RequiredStatus status);

        bool isAutoValue() const;  // Is it an auto-incrementing or identity column?
        void setAutoValue(bool autoVal);

        bool isReadOnly() const;  // Is the field read-only (e.g., computed column, not updatable)?
        void setReadOnly(bool ro);

        SqlValue defaultValue() const;  // Default value as defined in the database schema
        void setDefaultValue(const SqlValue& value);

        // Status flags
        bool isValid() const;  // Is the field metadata considered valid/complete? (e.g., name is not empty)

        bool isGenerated() const;  // Is the field a generated column (e.g. STORED/VIRTUAL in MySQL)?
        void setGenerated(bool generated);

        // Key Information
        bool isPrimaryKeyPart() const;
        void setPrimaryKeyPart(bool is_pk);

        bool isForeignKeyPart() const;
        void setForeignKeyPart(bool is_fk);

        // Foreign Key Details (if applicable)
        std::optional<std::string> referencedTableName() const;
        void setReferencedTableName(const std::optional<std::string>& name);
        std::optional<std::string> referencedColumnName() const;
        void setReferencedColumnName(const std::optional<std::string>& name);

        // Collation
        std::optional<std::string> collationName() const;
        void setCollationName(const std::optional<std::string>& name);

        // For fields derived from expressions or aliased in SELECT statements
        bool isExpression() const;  // True if this field is the result of an expression
        void setIsExpression(bool is_expr);
        std::optional<std::string> aliasName() const;  // Alias given to this field/expression in a query
        void setAliasName(const std::optional<std::string>& alias);

        // Origin Information (if the field comes from a base table column in a view or complex query)
        std::optional<std::string> baseTableName() const;
        void setBaseTableName(const std::optional<std::string>& name);
        std::optional<std::string> baseColumnName() const;
        void setBaseColumnName(const std::optional<std::string>& name);
        std::optional<std::string> baseSchemaName() const;
        void setBaseSchemaName(const std::optional<std::string>& name);

        // Generic metadata storage (e.g., for driver-specific flags or properties)
        std::any metaData() const;               // Get custom metadata
        void setMetaData(const std::any& data);  // Set custom metadata

      private:
        // Direct members
        std::string m_name;
        SqlValue m_value;  // The actual data value
        SqlValueType m_type_enum;
        std::string m_database_type_name;
        int m_driver_type_id;  // Driver-specific internal type enum value

        int m_length;     // Max length (chars for string, display for number)
        int m_precision;  // Total digits for numeric, or sub-second precision for time
        int m_scale;      // Digits after decimal for numeric

        RequiredStatus m_required_status;
        bool m_is_auto_value;  // Identity, auto_increment
        bool m_is_read_only;   // e.g. computed column
        SqlValue m_default_value;
        bool m_is_generated;  // STORED/VIRTUAL generated column

        bool m_is_primary_key_part;
        bool m_is_foreign_key_part;
        std::optional<std::string> m_referenced_table_name;
        std::optional<std::string> m_referenced_column_name;

        std::optional<std::string> m_collation_name;

        bool m_is_expression;
        std::optional<std::string> m_alias_name;
        std::optional<std::string> m_base_table_name;
        std::optional<std::string> m_base_column_name;
        std::optional<std::string> m_base_schema_name;

        std::any m_custom_meta_data;
    };

}  // namespace cpporm_sqldriver