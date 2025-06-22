// MySqlTransport/Source/mysql_transport_statement_query.cpp
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
                setErrorFromMySQL(m_connection->getNativeHandle(), "mysql_real_query failed for utility command: " + m_original_query);
                return nullptr;
            }
            res_handle = mysql_store_result(m_connection->getNativeHandle());
            if (!res_handle && mysql_errno(m_connection->getNativeHandle()) != 0) {
                setErrorFromMySQL(m_connection->getNativeHandle(), "mysql_store_result failed for utility command: " + m_original_query);
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

            // --- BEGIN Revised cleanup logic for prepared statements ---
            if (m_is_prepared && m_stmt_handle) {
                // 1. Free any result currently associated with the statement handle
                //    This is important if the statement was used before and its result wasn't fully consumed/freed.
                if (mysql_stmt_free_result(m_stmt_handle)) {
                    // This call can return an error if no result was pending, which is fine.
                    // We only care about unexpected errors.
                    if (mysql_stmt_errno(m_stmt_handle) != 0 && mysql_stmt_errno(m_stmt_handle) != CR_NO_RESULT_SET) {
                        // Log this error, but proceed with execution attempt.
                        // setErrorFromMySQL(reinterpret_cast<MYSQL*>(m_stmt_handle), "Error freeing previous result explicitly");
                    }
                }

                // 2. Loop through any *additional* pending results from a previous multi-result execution
                //    This ensures the statement handle is ready for a new mysql_stmt_execute().
                //    mysql_stmt_next_result() advances to the next result.
                //    If it returns 0, there was another result set, which we also need to free.
                //    If it returns -1, no more results.
                //    If it returns >0, an error occurred.
                int next_result_status;
                while ((next_result_status = mysql_stmt_next_result(m_stmt_handle)) == 0) {
                    // Successfully advanced to another result set. We need to free it as well
                    // if we are not going to process it.
                    if (mysql_stmt_free_result(m_stmt_handle)) {
                        if (mysql_stmt_errno(m_stmt_handle) != 0 && mysql_stmt_errno(m_stmt_handle) != CR_NO_RESULT_SET) {
                            // Log error
                        }
                    }
                }
                if (next_result_status > 0) {  // An error occurred while trying to advance
                    setErrorFromMySQL(reinterpret_cast<MYSQL*>(m_stmt_handle), "Error consuming previous multiple results");
                    return nullptr;
                }
                // After the loop, next_result_status is -1 (no more results) or an error was handled.
            }
            // --- END Revised cleanup logic ---

            if (mysql_stmt_execute(m_stmt_handle) != 0) {
                setErrorFromMySQL(reinterpret_cast<MYSQL*>(m_stmt_handle), "mysql_stmt_execute failed in executeQuery");
                return nullptr;
            }

            res_handle = mysql_stmt_result_metadata(m_stmt_handle);
            if (!res_handle) {
                if (mysql_stmt_errno(m_stmt_handle) != 0) {
                    setErrorFromMySQL(reinterpret_cast<MYSQL*>(m_stmt_handle), "mysql_stmt_result_metadata failed");
                    return nullptr;
                } else if (mysql_stmt_field_count(m_stmt_handle) == 0) {
                    // 合法：查询未产生列
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