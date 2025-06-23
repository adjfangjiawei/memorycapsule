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
            if (!prepare()) {
                return std::nullopt;
            }
        }
        clearError();
        m_affected_rows = 0;
        m_last_insert_id = 0;
        m_warning_count = 0;

        if (mysql_stmt_execute(m_stmt_handle) != 0) {
            setErrorFromStatementHandle("mysql_stmt_execute failed");
            return std::nullopt;
        }

        m_affected_rows = mysql_stmt_affected_rows(m_stmt_handle);
        m_last_insert_id = mysql_stmt_insert_id(m_stmt_handle);

        // 更正：使用 mysql_warning_count 并从连接句柄获取
        if (m_connection && m_connection->getNativeHandle()) {
            m_warning_count = mysql_warning_count(m_connection->getNativeHandle());
        }

        int status;
        do {
            MYSQL_RES* meta = mysql_stmt_result_metadata(m_stmt_handle);
            if (meta) {
                mysql_free_result(meta);
            } else {
                if (mysql_stmt_errno(m_stmt_handle) != 0) {
                    // 通常这里的错误会被 mysql_stmt_execute 捕获，这里可以忽略或记录日志
                }
            }
            status = mysql_stmt_next_result(m_stmt_handle);
            if (status > 0) {  // error
                setErrorFromStatementHandle("Error in mysql_stmt_next_result after DML");
                return std::nullopt;
            }
        } while (status == 0);

        if (status == -1) {
            if (mysql_stmt_errno(m_stmt_handle) != 0) {
                setErrorFromStatementHandle("Error after processing all results in DML execute");
                return std::nullopt;
            }
        }

        return m_affected_rows;
    }

}  // namespace cpporm_mysql_transport