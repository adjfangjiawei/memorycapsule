// cpporm_mysql_transport/mysql_transport_statement_execute.cpp
#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_connection.h"  // For connection handle
#include "cpporm_mysql_transport/mysql_transport_statement.h"
// No CR_NO_MORE_RESULTS needed here based on current logic.

namespace cpporm_mysql_transport {

    std::optional<my_ulonglong> MySqlTransportStatement::execute() {
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
            setErrorFromMySQL();
            return std::nullopt;
        }

        m_affected_rows = mysql_stmt_affected_rows(m_stmt_handle);
        m_last_insert_id = mysql_stmt_insert_id(m_stmt_handle);
        if (m_connection && m_connection->getNativeHandle()) {
            m_warning_count = mysql_warning_count(m_connection->getNativeHandle());
        }

        int status;
        do {
            MYSQL_RES* meta = mysql_stmt_result_metadata(m_stmt_handle);
            if (meta) {
                mysql_free_result(meta);
            }
            // It's crucial to free any *stored* result *before* calling mysql_stmt_next_result
            // if mysql_stmt_store_result was used.
            // MySqlTransportResult handles this for results it manages.
            // If this `execute()` call itself could have produced a result that
            // `mysql_stmt_store_result` might have been called on internally by some driver logic
            // (not typical for a simple execute of DML), then that would need freeing.
            // For now, assume direct `mysql_stmt_next_result` is for advancing past un-stored results.
            if (mysql_stmt_free_result(m_stmt_handle)) {
                // This attempts to free a result stored by mysql_stmt_store_result.
                // If no such result, it does nothing or returns error.
                // Only check error if it's unexpected.
                if (mysql_stmt_errno(m_stmt_handle) != 0 && mysql_stmt_errno(m_stmt_handle) != CR_NO_RESULT_SET) {
                    // CR_NO_RESULT_SET (if defined) means it's fine if no result was stored.
                    // setErrorFromMySQL(); // Potentially log or handle this.
                }
            }

            status = mysql_stmt_next_result(m_stmt_handle);
            if (status > 0) {
                setErrorFromMySQL();
                return std::nullopt;
            }
        } while (status == 0);

        if (status == -1) {
            if (mysql_stmt_errno(m_stmt_handle) != 0) {
                setErrorFromMySQL();
                return std::nullopt;
            }
        }

        return m_affected_rows;
    }

}  // namespace cpporm_mysql_transport