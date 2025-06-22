// SqlDriver/Source/sql_database_metadata_features.cpp
#include <vector>  // For std::vector return types

#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_enums.h"  // For Feature, ISqlDriverNs::TableType
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_index.h"
#include "sqldriver/sql_record.h"
#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    // --- 元数据访问 ---
    std::vector<std::string> SqlDatabase::tables(ISqlDriverNs::TableType type, const std::string& schemaFilter, const std::string& tableNameFilter) const {
        if (!isOpen() || !m_driver) {  // 增加对 m_driver 的检查
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::tables");
            return {};
        }
        auto result = m_driver->tables(type, schemaFilter, tableNameFilter);
        updateLastErrorFromDriver();
        return result;
    }

    std::vector<std::string> SqlDatabase::schemas(const std::string& schemaFilter) const {
        if (!isOpen() || !m_driver) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::schemas");
            return {};
        }
        auto result = m_driver->schemas(schemaFilter);
        updateLastErrorFromDriver();
        return result;
    }

    SqlRecord SqlDatabase::record(const std::string& tablename, const std::string& schema) const {
        if (!isOpen() || !m_driver) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::record");
            return SqlRecord();
        }
        auto result = m_driver->record(tablename, schema);
        updateLastErrorFromDriver();
        return result;
    }

    SqlIndex SqlDatabase::primaryIndex(const std::string& tablename, const std::string& schema) const {
        if (!isOpen() || !m_driver) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::primaryIndex");
            return SqlIndex();
        }
        auto result = m_driver->primaryIndex(tablename, schema);
        updateLastErrorFromDriver();
        return result;
    }

    std::vector<SqlIndex> SqlDatabase::indexes(const std::string& tablename, const std::string& schema) const {
        if (!isOpen() || !m_driver) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::indexes");
            return {};
        }
        auto result = m_driver->indexes(tablename, schema);
        updateLastErrorFromDriver();
        return result;
    }

    // --- 特性支持和版本信息 ---
    bool SqlDatabase::hasFeature(Feature feature) const {
        if (!m_driver) return false;
        return m_driver->hasFeature(feature);
    }

    SqlValue SqlDatabase::nativeHandle() const {
        if (!isOpen() || !m_driver) return SqlValue();
        return m_driver->nativeHandle();
    }

    std::string SqlDatabase::databaseProductVersion() const {
        if (!isOpen() || !m_driver) return "";
        return m_driver->databaseProductVersion();
    }

    std::string SqlDatabase::driverVersion() const {
        if (!m_driver) return "";  // 如果没有驱动，返回空
        return m_driver->driverVersion();
    }

    // --- 序列 ---
    SqlValue SqlDatabase::nextSequenceValue(const std::string& sequenceName, const std::string& schema) {
        if (!isOpen() || !m_driver) {  // 增加对 m_driver 的检查
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available for nextSequenceValue.", "SqlDatabase::nextSequenceValue");
            return SqlValue();
        }
        SqlValue val = m_driver->nextSequenceValue(sequenceName, schema);
        updateLastErrorFromDriver();
        return val;
    }

}  // namespace cpporm_sqldriver