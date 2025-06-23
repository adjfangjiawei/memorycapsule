#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"

namespace cpporm_mysql_transport {

    std::unique_ptr<MySqlTransportResult> MySqlTransportStatement::executeQuery() {
        if (!m_connection || !m_connection->getNativeHandle()) {
            setError(MySqlTransportError::Category::ApiUsageError, "Connection or native handle not available for executeQuery.");
            return nullptr;
        }

        clearError();
        m_affected_rows = 0;
        m_last_insert_id = 0;
        m_warning_count = 0;

        MYSQL_RES* res_handle = nullptr;

        if (m_is_utility_command) {
            if (mysql_real_query(m_connection->getNativeHandle(), m_original_query.c_str(), m_original_query.length()) != 0) {
                setErrorFromConnectionHandle(m_connection->getNativeHandle(), "mysql_real_query failed for utility command: " + m_original_query);
                return nullptr;
            }
            res_handle = mysql_store_result(m_connection->getNativeHandle());
            if (!res_handle && mysql_errno(m_connection->getNativeHandle()) != 0) {
                setErrorFromConnectionHandle(m_connection->getNativeHandle(), "mysql_store_result failed for utility command: " + m_original_query);
                return nullptr;
            }
            m_affected_rows = mysql_affected_rows(m_connection->getNativeHandle());
            m_last_insert_id = mysql_insert_id(m_connection->getNativeHandle());

        } else {
            if (!m_stmt_handle) {
                setError(MySqlTransportError::Category::ApiUsageError, "Statement handle not initialized for executeQuery (prepared path).");
                return nullptr;
            }
            if (!m_is_prepared) {
                if (!prepare()) {
                    return nullptr;
                }
            }

            while (mysql_stmt_next_result(m_stmt_handle) == 0);

            if (mysql_stmt_execute(m_stmt_handle) != 0) {
                setErrorFromStatementHandle("mysql_stmt_execute failed in executeQuery");
                return nullptr;
            }

            res_handle = mysql_stmt_result_metadata(m_stmt_handle);
            if (!res_handle) {
                if (mysql_stmt_errno(m_stmt_handle) != 0) {
                    setErrorFromStatementHandle("mysql_stmt_result_metadata failed");
                    return nullptr;
                } else if (mysql_stmt_field_count(m_stmt_handle) == 0) {
                    // 合法情况
                } else {
                    setError(MySqlTransportError::Category::QueryError, "Failed to get result metadata (prepared), but fields were expected.");
                    return nullptr;
                }
            }
            m_affected_rows = mysql_stmt_affected_rows(m_stmt_handle);
            m_last_insert_id = mysql_stmt_insert_id(m_stmt_handle);
        }

        if (m_connection && m_connection->getNativeHandle()) {
            m_warning_count = mysql_warning_count(m_connection->getNativeHandle());
        }

        if (m_is_utility_command) {
            return std::make_unique<MySqlTransportResult>(res_handle, m_last_error);
        } else {
            return std::make_unique<MySqlTransportResult>(this, res_handle, m_last_error);
        }
    }

}  // namespace cpporm_mysql_transport