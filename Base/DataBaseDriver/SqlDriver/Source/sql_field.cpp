// SqlDriver/Source/sql_field.cpp
#include "sqldriver/sql_field.h"

namespace cpporm_sqldriver {

    // --- Constructors ---
    SqlField::SqlField(const std::string& name, SqlValueType type, const std::string& db_type_name)
        : m_name(name),
          m_value(),  // Default constructs to null SqlValue
          m_type_enum(type),
          m_database_type_name(db_type_name),
          m_driver_type_id(0),  // Default driver type ID
          m_length(-1),         // Unspecified
          m_precision(-1),      // Unspecified
          m_scale(-1),          // Unspecified
          m_required_status(RequiredStatus::Unknown),
          m_is_auto_value(false),
          m_is_read_only(false),
          m_default_value(),  // Default constructs to null SqlValue
          m_is_generated(false),
          m_is_primary_key_part(false),
          m_is_foreign_key_part(false),
          m_is_expression(false)
    // optionals are default constructed to std::nullopt
    {
    }

    SqlField::~SqlField() = default;

    // --- Copy and Move semantics ---
    SqlField::SqlField(const SqlField& other) = default;  // Use default for all members
    SqlField& SqlField::operator=(const SqlField& other) = default;
    SqlField::SqlField(SqlField&& other) noexcept = default;
    SqlField& SqlField::operator=(SqlField&& other) noexcept = default;

    // --- Basic Properties ---
    std::string SqlField::name() const {
        return m_name;
    }
    void SqlField::setName(const std::string& name) {
        m_name = name;
    }

    SqlValue SqlField::value() const {
        return m_value;
    }
    void SqlField::setValue(const SqlValue& value) {
        m_value = value;
    }
    void SqlField::clearValue() {
        m_value.clear();
    }  // Or m_value = SqlValue();
    bool SqlField::isNullInValue() const {
        return m_value.isNull();
    }

    // --- Type Information ---
    SqlValueType SqlField::type() const {
        return m_type_enum;
    }
    void SqlField::setType(SqlValueType type) {
        m_type_enum = type;
    }

    std::string SqlField::databaseTypeName() const {
        return m_database_type_name;
    }
    void SqlField::setDatabaseTypeName(const std::string& name) {
        m_database_type_name = name;
    }

    int SqlField::driverType() const {
        return m_driver_type_id;
    }
    void SqlField::setDriverType(int typeId) {
        m_driver_type_id = typeId;
    }

    // --- Size and Precision ---
    int SqlField::length() const {
        return m_length;
    }
    void SqlField::setLength(int len) {
        m_length = len;
    }

    int SqlField::precision() const {
        return m_precision;
    }
    void SqlField::setPrecision(int prec) {
        m_precision = prec;
    }

    int SqlField::scale() const {
        return m_scale;
    }
    void SqlField::setScale(int s) {
        m_scale = s;
    }

    // --- Constraints and Attributes ---
    RequiredStatus SqlField::requiredStatus() const {
        return m_required_status;
    }
    void SqlField::setRequiredStatus(RequiredStatus status) {
        m_required_status = status;
    }

    bool SqlField::isAutoValue() const {
        return m_is_auto_value;
    }
    void SqlField::setAutoValue(bool autoVal) {
        m_is_auto_value = autoVal;
    }

    bool SqlField::isReadOnly() const {
        return m_is_read_only;
    }
    void SqlField::setReadOnly(bool ro) {
        m_is_read_only = ro;
    }

    SqlValue SqlField::defaultValue() const {
        return m_default_value;
    }
    void SqlField::setDefaultValue(const SqlValue& value) {
        m_default_value = value;
    }

    // --- Status flags ---
    bool SqlField::isValid() const {
        // A field is minimally valid if it has a name. Other checks could be added.
        return !m_name.empty();
    }

    bool SqlField::isGenerated() const {
        return m_is_generated;
    }
    void SqlField::setGenerated(bool generated) {
        m_is_generated = generated;
    }

    // --- Key Information ---
    bool SqlField::isPrimaryKeyPart() const {
        return m_is_primary_key_part;
    }
    void SqlField::setPrimaryKeyPart(bool is_pk) {
        m_is_primary_key_part = is_pk;
    }

    bool SqlField::isForeignKeyPart() const {
        return m_is_foreign_key_part;
    }
    void SqlField::setForeignKeyPart(bool is_fk) {
        m_is_foreign_key_part = is_fk;
    }

    // --- Foreign Key Details ---
    std::optional<std::string> SqlField::referencedTableName() const {
        return m_referenced_table_name;
    }
    void SqlField::setReferencedTableName(const std::optional<std::string>& name) {
        m_referenced_table_name = name;
    }
    std::optional<std::string> SqlField::referencedColumnName() const {
        return m_referenced_column_name;
    }
    void SqlField::setReferencedColumnName(const std::optional<std::string>& name) {
        m_referenced_column_name = name;
    }

    // --- Collation ---
    std::optional<std::string> SqlField::collationName() const {
        return m_collation_name;
    }
    void SqlField::setCollationName(const std::optional<std::string>& name) {
        m_collation_name = name;
    }

    // --- Expression/Alias Information ---
    bool SqlField::isExpression() const {
        return m_is_expression;
    }
    void SqlField::setIsExpression(bool is_expr) {
        m_is_expression = is_expr;
    }
    std::optional<std::string> SqlField::aliasName() const {
        return m_alias_name;
    }
    void SqlField::setAliasName(const std::optional<std::string>& alias) {
        m_alias_name = alias;
    }

    // --- Origin Information ---
    std::optional<std::string> SqlField::baseTableName() const {
        return m_base_table_name;
    }
    void SqlField::setBaseTableName(const std::optional<std::string>& name) {
        m_base_table_name = name;
    }
    std::optional<std::string> SqlField::baseColumnName() const {
        return m_base_column_name;
    }
    void SqlField::setBaseColumnName(const std::optional<std::string>& name) {
        m_base_column_name = name;
    }
    std::optional<std::string> SqlField::baseSchemaName() const {
        return m_base_schema_name;
    }
    void SqlField::setBaseSchemaName(const std::optional<std::string>& name) {
        m_base_schema_name = name;
    }

    // --- Generic metadata ---
    std::any SqlField::metaData() const {
        return m_custom_meta_data;
    }
    void SqlField::setMetaData(const std::any& data) {
        m_custom_meta_data = data;
    }

}  // namespace cpporm_sqldriver