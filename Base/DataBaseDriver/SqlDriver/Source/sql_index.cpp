// SqlDriver/Source/sql_index.cpp
#include "sqldriver/sql_index.h"

namespace cpporm_sqldriver {

    SqlIndex::SqlIndex(const std::string& name, const std::string& tableName, const std::string& schemaName)
        : m_name(name),
          m_table_name(tableName),
          m_schema_name(schemaName),
          m_is_unique(false),
          m_is_primary_key(false),
          m_is_functional(false),
          m_type_method("BTREE")  // Common default
    {
    }

    SqlIndex::~SqlIndex() = default;

    // --- Copy and Move semantics ---
    SqlIndex::SqlIndex(const SqlIndex& other) = default;
    SqlIndex& SqlIndex::operator=(const SqlIndex& other) = default;
    SqlIndex::SqlIndex(SqlIndex&& other) noexcept = default;
    SqlIndex& SqlIndex::operator=(SqlIndex&& other) noexcept = default;

    // --- Basic Properties ---
    std::string SqlIndex::name() const {
        return m_name;
    }
    void SqlIndex::setName(const std::string& name) {
        m_name = name;
    }

    std::string SqlIndex::tableName() const {
        return m_table_name;
    }
    void SqlIndex::setTableName(const std::string& name) {
        m_table_name = name;
    }

    std::string SqlIndex::schemaName() const {
        return m_schema_name;
    }
    void SqlIndex::setSchemaName(const std::string& schema) {
        m_schema_name = schema;
    }

    // --- Index Characteristics ---
    bool SqlIndex::isUnique() const {
        return m_is_unique;
    }
    void SqlIndex::setUnique(bool unique) {
        m_is_unique = unique;
    }

    bool SqlIndex::isPrimaryKey() const {
        return m_is_primary_key;
    }
    void SqlIndex::setPrimaryKey(bool pk) {
        m_is_primary_key = pk;
    }

    bool SqlIndex::isFunctional() const {
        return m_is_functional;
    }
    void SqlIndex::setFunctional(bool functional) {
        m_is_functional = functional;
    }

    std::string SqlIndex::typeMethod() const {
        return m_type_method;
    }
    void SqlIndex::setTypeMethod(const std::string& method) {
        m_type_method = method;
    }

    // --- Columns in the Index ---
    void SqlIndex::appendColumn(const IndexColumnDefinition& colDef) {
        m_columns.push_back(colDef);
    }

    void SqlIndex::appendColumn(const std::string& fieldName, IndexSortOrder order, const std::optional<std::string>& expression, IndexNullsPosition nulls, const std::optional<std::string>& opClass) {
        IndexColumnDefinition colDef;
        colDef.fieldName = fieldName;
        colDef.sortOrder = order;
        colDef.expression = expression;
        colDef.nullsPosition = nulls;
        colDef.opClass = opClass;
        m_columns.push_back(colDef);
    }

    int SqlIndex::columnCount() const {
        return static_cast<int>(m_columns.size());
    }

    IndexColumnDefinition SqlIndex::column(int i) const {
        if (i >= 0 && static_cast<size_t>(i) < m_columns.size()) {
            return m_columns[static_cast<size_t>(i)];
        }
        // Consider throwing std::out_of_range or returning a "null" IndexColumnDefinition
        return IndexColumnDefinition{};
    }
    const std::vector<IndexColumnDefinition>& SqlIndex::columns() const {
        return m_columns;
    }

    // --- Advanced Index Properties ---
    std::string SqlIndex::condition() const {
        return m_condition;
    }
    void SqlIndex::setCondition(const std::string& cond) {
        m_condition = cond;
    }

    std::vector<std::string> SqlIndex::includedColumnNames() const {
        return m_included_columns;
    }
    void SqlIndex::addIncludedColumn(const std::string& columnName) {
        m_included_columns.push_back(columnName);
    }
    void SqlIndex::setIncludedColumns(const std::vector<std::string>& columnNames) {
        m_included_columns = columnNames;
    }

    // --- Driver/DB specific options ---
    std::map<std::string, SqlValue> SqlIndex::options() const {
        return m_options;
    }
    void SqlIndex::setOption(const std::string& optionName, const SqlValue& value) {
        m_options[optionName] = value;
    }
    SqlValue SqlIndex::option(const std::string& optionName) const {
        auto it = m_options.find(optionName);
        if (it != m_options.end()) {
            return it->second;
        }
        return SqlValue();  // Null SqlValue if option not found
    }

    void SqlIndex::clear() {
        m_name.clear();
        m_table_name.clear();
        m_schema_name.clear();
        m_is_unique = false;
        m_is_primary_key = false;
        m_is_functional = false;
        m_type_method.clear();  // Or set to default like "BTREE"
        m_columns.clear();
        m_condition.clear();
        m_included_columns.clear();
        m_options.clear();
    }

}  // namespace cpporm_sqldriver