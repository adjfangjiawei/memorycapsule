// SqlDriver/Source/mysql/mysql_specific_driver_metadata.cpp
#include <algorithm>  // For std::sort, std::unique
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_connection.h"  // For isOpen through driver
#include "cpporm_mysql_transport/mysql_transport_metadata.h"    // 确保 MySqlTransportMetadata 的完整定义可见
#include "sqldriver/mysql/mysql_driver_helper.h"                // For metadata converters
#include "sqldriver/mysql/mysql_specific_driver.h"

namespace cpporm_sqldriver {

    std::vector<std::string> MySqlSpecificDriver::tables(ISqlDriverNs::TableType type, const std::string& schemaFilter, const std::string& tableNameFilter) const {
        if (!isOpen() || !m_transport_metadata) {
            // 如果驱动未打开或元数据对象未初始化，则返回空列表
            // const 方法不应修改 m_last_error_cache，除非它是 mutable
            // 调用者应检查 isOpen() 和/或 lastError()
            return {};
        }
        std::string current_schema = resolveSchemaName(schemaFilter);
        std::optional<std::vector<std::string>> result_opt;

        // 清除之前的错误，因为这是一个新的操作
        m_last_error_cache = SqlError();

        switch (type) {
            case ISqlDriverNs::TableType::Tables:
                result_opt = m_transport_metadata->listTables(current_schema, tableNameFilter);
                break;
            case ISqlDriverNs::TableType::Views:
                result_opt = m_transport_metadata->listViews(current_schema, tableNameFilter);
                break;
            case ISqlDriverNs::TableType::All:  // 获取表和视图
                {
                    std::vector<std::string> all_list;
                    auto tables_opt = m_transport_metadata->listTables(current_schema, tableNameFilter);
                    if (tables_opt) {
                        all_list.insert(all_list.end(), tables_opt->begin(), tables_opt->end());
                    } else {
                        // 如果 listTables 失败，获取错误
                        m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_metadata->getLastError());
                        // 如果一个失败，可能不继续尝试获取视图，或者由调用者决定
                    }

                    auto views_opt = m_transport_metadata->listViews(current_schema, tableNameFilter);
                    if (views_opt) {
                        all_list.insert(all_list.end(), views_opt->begin(), views_opt->end());
                    } else if (!tables_opt.has_value()) {  // 如果获取表也失败了
                                                           // 错误已经从 listTables 获取，不再覆盖
                    } else {                               // 表获取成功，但视图获取失败
                        m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_metadata->getLastError());
                    }

                    std::sort(all_list.begin(), all_list.end());
                    all_list.erase(std::unique(all_list.begin(), all_list.end()), all_list.end());
                    return all_list;  // 返回组合列表
                }
            case ISqlDriverNs::TableType::SystemTables:
                // MySQL information_schema 可以被视为系统表所在之处
                // 如果 schemaFilter 为空，则可以列出 information_schema 中的表
                if (current_schema.empty() || current_schema == "information_schema") {
                    result_opt = m_transport_metadata->listTables("information_schema", tableNameFilter);
                } else {  // 如果指定了其他 schema，则系统表为空
                    return {};
                }
                break;
            default:  // 其他类型目前不支持
                return {};
        }

        if (result_opt) {
            return *result_opt;
        } else {
            // 如果 result_opt 为空 (std::nullopt)，表示 transport 层操作失败
            // 从 transport metadata 获取错误并转换
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_metadata->getLastError());
            return {};
        }
    }

    std::vector<std::string> MySqlSpecificDriver::schemas(const std::string& schemaFilter) const {
        if (!isOpen() || !m_transport_metadata) {
            return {};
        }
        m_last_error_cache = SqlError();  // 清除错误
        auto result_opt = m_transport_metadata->listDatabases(schemaFilter);
        if (result_opt) {
            return *result_opt;
        } else {
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_metadata->getLastError());
            return {};
        }
    }

    SqlRecord MySqlSpecificDriver::record(const std::string& tablename, const std::string& schema) const {
        if (!isOpen() || !m_transport_metadata || tablename.empty()) {
            return SqlRecord();  // 返回空记录
        }
        m_last_error_cache = SqlError();  // 清除错误
        std::string current_schema = resolveSchemaName(schema);

        auto transport_fields_opt = m_transport_metadata->getTableColumns(tablename, current_schema);
        if (transport_fields_opt) {
            return mysql_helper::metasToSqlRecord(*transport_fields_opt);
        } else {
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_metadata->getLastError());
            return SqlRecord();
        }
    }

    SqlIndex MySqlSpecificDriver::primaryIndex(const std::string& tablename, const std::string& schema) const {
        if (!isOpen() || !m_transport_metadata || tablename.empty()) {
            return SqlIndex();  // 返回空索引
        }
        m_last_error_cache = SqlError();  // 清除错误
        std::string current_schema = resolveSchemaName(schema);

        auto transport_pk_opt = m_transport_metadata->getPrimaryIndex(tablename, current_schema);
        if (transport_pk_opt) {
            return mysql_helper::metaToSqlIndex(*transport_pk_opt);
        } else {
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_metadata->getLastError());
            return SqlIndex();
        }
    }

    std::vector<SqlIndex> MySqlSpecificDriver::indexes(const std::string& tablename, const std::string& schema) const {
        if (!isOpen() || !m_transport_metadata || tablename.empty()) {
            return {};
        }
        m_last_error_cache = SqlError();  // 清除错误
        std::string current_schema = resolveSchemaName(schema);

        auto transport_indexes_opt = m_transport_metadata->getTableIndexes(tablename, current_schema);
        if (transport_indexes_opt) {
            return mysql_helper::metasToSqlIndexes(*transport_indexes_opt);
        } else {
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_metadata->getLastError());
            return {};
        }
    }

}  // namespace cpporm_sqldriver