#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"

namespace cpporm_mysql_transport {

    std::optional<my_ulonglong> MySqlTransportStatement::execute() {
        if (m_is_utility_command) {
            setError(MySqlTransportError::Category::ApiUsageError, "Utility commands (like SHOW) should be run via executeQuery, not execute.");
            return std::nullopt;
        }

        if (!m_stmt_handle) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle not initialized for execute.");
            return std::nullopt;
        }
        if (!m_is_prepared) {
            if (!prepare()) {  // prepare() already checks for utility command
                return std::nullopt;
            }
        }
        clearError();
        m_affected_rows = 0;
        m_last_insert_id = 0;
        m_warning_count = 0;

        if (mysql_stmt_execute(m_stmt_handle) != 0) {
            setErrorFromMySQL(reinterpret_cast<MYSQL*>(m_stmt_handle), "mysql_stmt_execute failed");
            return std::nullopt;
        }

        m_affected_rows = mysql_stmt_affected_rows(m_stmt_handle);
        m_last_insert_id = mysql_stmt_insert_id(m_stmt_handle);
        if (m_connection && m_connection->getNativeHandle()) {
            m_warning_count = mysql_warning_count(m_connection->getNativeHandle());
        }

        // DML 通常不会返回结果集，但如果存储过程执行DML并返回状态，可能需要处理
        // mysql_stmt_next_result 循环是为了清除可能由某些语句（如多语句查询或某些存储过程）返回的任何额外结果集或状态。
        // 对于简单的DML，这通常是不必要的，但为了通用性而保留。
        int status;
        do {
            // 检查语句句柄上是否有结果集元数据，通常DML没有
            MYSQL_RES* meta = mysql_stmt_result_metadata(m_stmt_handle);
            if (meta) {
                mysql_free_result(meta);  // 如果有，则释放
            } else {
                // 如果 mysql_stmt_result_metadata 返回 NULL，检查是否有错误
                if (mysql_stmt_errno(m_stmt_handle) != 0) {
                    // setErrorFromMySQL(reinterpret_cast<MYSQL*>(m_stmt_handle), "Error after DML checking for metadata");
                    // 错误可能已经被 mysql_stmt_execute 设置，这里可能是重复的
                    // return std::nullopt; // 可能过于严格
                }
            }
            // 尝试前进到下一个结果集（如果有）
            status = mysql_stmt_next_result(m_stmt_handle);
            if (status > 0) {  // error
                setErrorFromMySQL(reinterpret_cast<MYSQL*>(m_stmt_handle), "Error in mysql_stmt_next_result after DML");
                return std::nullopt;
            }
        } while (status == 0);  // status == 0 表示有更多结果集

        // status == -1 表示没有更多结果集了，这是正常的结束
        if (status == -1) {
            if (mysql_stmt_errno(m_stmt_handle) != 0) {  // 再次检查错误
                setErrorFromMySQL(reinterpret_cast<MYSQL*>(m_stmt_handle), "Error after processing all results in DML execute");
                return std::nullopt;
            }
        }

        return m_affected_rows;
    }

}  // namespace cpporm_mysql_transport