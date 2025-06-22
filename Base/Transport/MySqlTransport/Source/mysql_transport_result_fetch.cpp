// cpporm_mysql_transport/mysql_transport_result_fetch.cpp
#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"  // For m_statement->getError()

namespace cpporm_mysql_transport {

    void MySqlTransportResult::clearCurrentRow() {
        if (!m_is_from_prepared_statement) {
            m_current_sql_row = nullptr;
            m_current_lengths = nullptr;
        } else {
            // For prepared statements, data is in m_output_data_buffers.
            // These buffers are reused by mysql_stmt_fetch.
            // m_current_row_idx is reset/incremented by fetchNextRow.
            // No explicit clearing of m_output_data_buffers needed here per fetch,
            // as mysql_stmt_fetch overwrites them.
        }
        // Resetting m_current_row_idx is handled by the calling fetch function if it fails
        // or before a new fetch sequence. Here, we only clear pointers for non-prepared.
    }

    bool MySqlTransportResult::fetchNextRow() {
        if (!m_is_valid) return false;
        // Clearing of current row data (especially for non-prepared)
        // should happen before attempting a new fetch or if fetch fails.
        // Let's call it at the beginning of this attempt.
        clearCurrentRow();

        if (m_is_from_prepared_statement) {
            if (!m_mysql_stmt_handle_for_fetch || m_fetched_all_from_stmt) {
                // If already fetched all or invalid handle, no more rows.
                return false;
            }

            int fetch_rc = mysql_stmt_fetch(m_mysql_stmt_handle_for_fetch);
            if (fetch_rc == 0) {  // Success
                m_current_row_idx++;
                return true;
            } else if (fetch_rc == MYSQL_NO_DATA) {  // No more rows
                m_fetched_all_from_stmt = true;
                m_current_row_idx = -1;  // Indicate no current valid row
                return false;
            } else if (fetch_rc == MYSQL_DATA_TRUNCATED) {  // Data truncated
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::DataError, "Data truncated during fetch.", mysql_stmt_errno(m_mysql_stmt_handle_for_fetch), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
                m_current_row_idx++;  // Row is still fetched
                return true;
            } else {  // Error
                if (m_statement)
                    m_error_collector = m_statement->getError();
                else if (m_mysql_stmt_handle_for_fetch)
                    m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_fetch failed.", mysql_stmt_errno(m_mysql_stmt_handle_for_fetch), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
                else
                    m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_fetch failed (no statement context).");
                m_current_row_idx = -1;  // Indicate no current valid row
                return false;
            }
        } else {  // From non-prepared (MYSQL_RES)
            if (!m_mysql_res_metadata) return false;
            m_current_sql_row = mysql_fetch_row(m_mysql_res_metadata);
            if (m_current_sql_row) {
                m_current_lengths = mysql_fetch_lengths(m_mysql_res_metadata);
                m_current_row_idx++;
                return true;
            } else {
                m_current_row_idx = -1;  // Indicate no current valid row
                if (m_mysql_res_metadata && m_mysql_res_metadata->handle && mysql_errno(m_mysql_res_metadata->handle) == 0 && mysql_eof(m_mysql_res_metadata)) {
                    // Normal EOF
                } else if (m_mysql_res_metadata && m_mysql_res_metadata->handle) {
                    m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_fetch_row failed.", mysql_errno(m_mysql_res_metadata->handle), mysql_sqlstate(m_mysql_res_metadata->handle), mysql_error(m_mysql_res_metadata->handle));
                } else if (!m_error_collector.isOk()) {
                    // Error already set by a previous operation
                } else {
                    m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "Unknown error during mysql_fetch_row or no more rows.");
                }
                return false;
            }
        }
    }

}  // namespace cpporm_mysql_transport