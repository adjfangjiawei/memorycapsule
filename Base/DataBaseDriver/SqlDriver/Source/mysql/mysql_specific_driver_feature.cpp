// SqlDriver/Source/mysql/mysql_specific_driver_feature.cpp
#include "cpporm_mysql_transport/mysql_transport_connection.h"  // For version checks
#include "sqldriver/mysql/mysql_specific_driver.h"

namespace cpporm_sqldriver {

    bool MySqlSpecificDriver::hasFeature(Feature feature) const {
        if (!m_transport_connection) {  // 如果 transport connection 未初始化，则大部分功能不可用
            switch (feature) {
                // 即使没有连接，某些元特性也可能为 true
                case Feature::PositionalPlaceholders:
                    return true;  // MySQL C API 支持 '?'
                // 其他依赖连接状态的特性应返回 false
                default:
                    return false;
            }
        }

        switch (feature) {
            case Feature::Transactions:
                return true;  // MySQL 支持事务 (通常使用 InnoDB 引擎)
            case Feature::QuerySize:
                return true;  // 可以通过 mysql_stmt_num_rows (预处理) 或 mysql_num_rows (非预处理) 获取行数
            case Feature::BLOB:
                return true;  // 支持 BLOB 类型
            case Feature::Unicode:
                return true;  // 假设连接已正确配置为 UTF-8 等
            case Feature::PreparedQueries:
                return true;  // MySQL C API 支持预处理语句
            case Feature::NamedPlaceholders:
                // C API 本身不支持命名占位符，但驱动程序可以通过 SqlResult 层模拟
                // 此处返回 false 表示 C API 层面不支持，模拟是在上层
                return false;
            case Feature::PositionalPlaceholders:
                return true;  // C API 支持 '?'
            case Feature::LastInsertId:
                return true;  // mysql_stmt_insert_id 或 mysql_insert_id
            case Feature::BatchOperations:
                // C API 本身不直接支持批处理的单一函数调用，但可以通过重复执行预处理语句来模拟
                // 返回 false 表示没有单一的、高效的 C API 级批处理。模拟是另一回事。
                return false;
            case Feature::MultipleResultSets:
                return true;  // 支持存储过程返回多个结果集 (mysql_stmt_next_result)
            case Feature::NamedSavepoints:
                return true;  // 支持 SAVEPOINT name, ROLLBACK TO SAVEPOINT name, RELEASE SAVEPOINT name
            case Feature::SchemaOperations:
                return true;  // 可以通过 SHOW DATABASES, SHOW TABLES 等获取
            case Feature::TransactionIsolationLevel:
                return true;  // 支持设置事务隔离级别
            case Feature::PingConnection:
                return true;  // mysql_ping
            case Feature::SimpleScrollOnError:
                // 默认 C API 结果集是仅向前的，除非使用 mysql_stmt_store_result 后进行 data_seek
                return false;
            case Feature::EventNotifications:
                return false;  // 不是标准的 MySQL C API 功能
            case Feature::FinishQuery:
                return true;  // mysql_stmt_free_result / mysql_free_result
            case Feature::LowPrecisionNumbers:
                // MySQL 可以返回高精度数字作为字符串，驱动可以控制此行为
                return true;
            case Feature::CancelQuery:
                // mysql_kill 可以终止连接/查询，但不是通用的取消操作
                return false;
            case Feature::InsertAndReturnId:
                // 通过 LastInsertId 间接支持，没有类似 PostgreSQL 的 RETURNING 子句
                // 许多驱动将此特性标记为 true，因为可以获取 ID
                return true;
            case Feature::ThreadSafe:
                // MySQL C API 本身是线程安全的，如果每个线程使用自己的 MYSQL 句柄
                // 并且 mysql_library_init/end 被正确调用。
                return true;
            case Feature::GetTypeInfo:
                return false;  // 没有直接的 ODBC 风格的 GetTypeInfo
            case Feature::SetQueryTimeout:
                // MySQL C API 有 MYSQL_OPT_READ_TIMEOUT, MYSQL_OPT_WRITE_TIMEOUT
                // 但通常不是针对单个查询的超时，而是套接字操作。
                // MYSQL_STMT_ATTR_UPDATE_MAX_LENGTH 可用于流式结果，但不完全是查询超时。
                // 严格来说，单个语句的执行超时不直接支持。
                return false;
            case Feature::StreamBlob:
                // mysql_stmt_send_long_data 用于发送大数据块
                // 读取 BLOB 通常是获取整个块或使用 mysql_fetch_fields_direct 配合 buffer
                // 直接的流式 API (如 C++ iostream) 需要驱动包装。
                // 如果指 C API 的分块能力，则部分为真。如果指高级流，则为 false。
                return false;  // 假设指高级流
            case Feature::CallableStatements:
                return true;  // 支持 CALL procedure_name(...)
            case Feature::BatchWithErrorDetails:
                // 如果 BatchOperations 为 false，这个也应为 false。
                // 如果模拟批处理，错误细节是每次执行的结果。
                return false;

            case Feature::SequenceOperations:
                if (isOpen() && m_transport_connection) {
                    // MySQL 8.0.0 开始支持原生序列。MariaDB 10.3。
                    unsigned long server_ver_num = m_transport_connection->getServerVersionNumber();
                    // server_ver_num 格式如 80023 for 8.0.23.
                    // 检查是否为 MySQL 8+ 或 MariaDB 10.3+
                    // 这是一个简化的检查，实际可能需要区分 MySQL 和 MariaDB 的版本字符串。
                    // 假设 m_transport_connection->getServerVersionString() 可用于区分。
                    std::string ver_str = m_transport_connection->getServerVersionString();
                    bool is_mariadb = (ver_str.find("MariaDB") != std::string::npos);

                    if (is_mariadb) {
                        // MariaDB: 主版本号 * 10000 + 次版本号 * 100 + 补丁级别
                        // 10.3.0 -> 100300
                        unsigned long mariadb_maj = server_ver_num / 10000;
                        unsigned long mariadb_min = (server_ver_num / 100) % 100;
                        if (mariadb_maj > 10 || (mariadb_maj == 10 && mariadb_min >= 3)) {
                            return true;
                        }
                    } else {                            // Assume MySQL
                        if (server_ver_num >= 80000) {  // MySQL 8.0.0
                            return true;
                        }
                    }
                }
                return false;  // 未连接或版本不支持

            case Feature::UpdatableCursors:
                return false;  // MySQL C API 通常不直接支持可更新游标

            default:
                return false;
        }
    }

    std::string MySqlSpecificDriver::databaseProductVersion() const {
        if (!isOpen() || !m_transport_connection) return "";
        return m_transport_connection->getServerVersionString();
    }

    std::string MySqlSpecificDriver::driverVersion() const {
        // 这应该是驱动程序本身的硬编码版本
        return "CppOrmSqlDriver-MySQL-1.0.3";  // 示例版本
    }

}  // namespace cpporm_sqldriver