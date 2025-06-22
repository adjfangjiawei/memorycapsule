// cpporm_mysql_transport/mysql_transport_statement_query.cpp
#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_connection.h"  // For connection handle
#include "cpporm_mysql_transport/mysql_transport_result.h"      // For std::make_unique<MySqlTransportResult>
#include "cpporm_mysql_transport/mysql_transport_statement.h"

namespace cpporm_mysql_transport {

    std::unique_ptr<MySqlTransportResult> MySqlTransportStatement::executeQuery() {
        if (!m_stmt_handle) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle not initialized for executeQuery.");
            return nullptr;
        }
        if (!m_is_prepared) {
            if (!prepare()) {
                return nullptr;
            }
        }
        clearError();
        m_affected_rows = 0;  // Typically 0 for SELECT, but reset
        m_last_insert_id = 0;
        m_warning_count = 0;

        // Similar to execute(), ensure any previous stored result is freed.
        // MySqlTransportResult's destructor should handle this.
        // If statement is reused without result destruction, this could be an issue.
        if (mysql_stmt_free_result(m_stmt_handle)) {
            if (mysql_stmt_errno(m_stmt_handle) != 0 && mysql_stmt_errno(m_stmt_handle) != CR_NO_RESULT_SET) {
                // setErrorFromMySQL(); // Potentially log or handle this.
            }
        }

        if (mysql_stmt_execute(m_stmt_handle) != 0) {
            setErrorFromMySQL();
            return nullptr;
        }

        MYSQL_RES* meta_res_handle = mysql_stmt_result_metadata(m_stmt_handle);

        if (!meta_res_handle) {
            if (mysql_stmt_errno(m_stmt_handle) != 0) {
                setErrorFromMySQL();
                return nullptr;
            } else if (mysql_stmt_field_count(m_stmt_handle) == 0) {
                // Valid case: query produced no columns (e.g., SELECT 1 WHERE FALSE).
                // MySqlTransportResult constructor should handle meta_res_handle being NULL
                // if field_count is 0 by creating an "empty but valid" result.
            } else {
                // field_count > 0 but metadata is NULL: this is an unexpected error state.
                setError(MySqlTransportError::Category::QueryError, "Failed to get result metadata after executeQuery, but fields were expected.");
                return nullptr;
            }
        }

        if (m_connection && m_connection->getNativeHandle()) {
            m_warning_count = mysql_warning_count(m_connection->getNativeHandle());
        }

        // MySqlTransportResult constructor for prepared statements will call mysql_stmt_store_result.
        return std::make_unique<MySqlTransportResult>(this, meta_res_handle, m_last_error);
    }

}  // namespace cpporm_mysql_transport