#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"

namespace cpporm_mysql_transport {

    void MySqlTransportResult::clearCurrentRow() {
        if (!m_is_from_prepared_statement) {
            m_current_sql_row = nullptr;
            m_current_lengths = nullptr;
        }
        // For prepared statements, data is in m_output_data_buffers. These are reused.
        // m_current_row_idx is reset/incremented by fetchNextRow.
    }

    bool MySqlTransportResult::fetchNextRow() {
        if (!m_is_valid) return false;
        clearCurrentRow();  // Clear previous row data first

        if (m_is_from_prepared_statement) {
            if (!m_mysql_stmt_handle_for_fetch || m_fetched_all_from_stmt) {
                return false;
            }

            int fetch_rc = mysql_stmt_fetch(m_mysql_stmt_handle_for_fetch);
            if (fetch_rc == 0) {  // Success
                m_current_row_idx++;
                return true;
            } else if (fetch_rc == MYSQL_NO_DATA) {  // No more rows
                m_fetched_all_from_stmt = true;
                m_current_row_idx = -1;
                return false;
            } else if (fetch_rc == MYSQL_DATA_TRUNCATED) {  // Data truncated
                // Set error on m_error_collector_owned
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::DataError, "Data truncated during fetch.", mysql_stmt_errno(m_mysql_stmt_handle_for_fetch), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
                m_current_row_idx++;  // Row is still fetched despite truncation
                return true;
            } else {                // Error
                if (m_statement) {  // If statement context available, try to get its error
                    m_error_collector_owned = m_statement->getError();
                    // If statement's error is still OK, then set a specific fetch error
                    if (m_error_collector_owned.isOk()) {
                        m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_fetch failed.", mysql_stmt_errno(m_mysql_stmt_handle_for_fetch), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
                    }
                } else if (m_mysql_stmt_handle_for_fetch) {  // No statement context, but have stmt handle
                    m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_fetch failed.", mysql_stmt_errno(m_mysql_stmt_handle_for_fetch), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
                } else {  // Fallback
                    m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_fetch failed (no statement context or handle).");
                }
                m_current_row_idx = -1;
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
                m_current_row_idx = -1;
                // Check if it's a real error or just end of data
                if (m_mysql_res_metadata->handle && mysql_errno(m_mysql_res_metadata->handle) != 0) {
                    m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_fetch_row failed.", mysql_errno(m_mysql_res_metadata->handle), mysql_sqlstate(m_mysql_res_metadata->handle), mysql_error(m_mysql_res_metadata->handle));
                } else if (m_mysql_res_metadata->handle && mysql_errno(m_mysql_res_metadata->handle) == 0 && mysql_eof(m_mysql_res_metadata)) {
                    // Normal EOF, m_error_collector_owned should remain OK
                } else {
                    // If m_error_collector_owned is already set (e.g., from a previous issue), don't overwrite.
                    // If it's currently OK, then this is unexpected.
                    if (m_error_collector_owned.isOk()) {
                        m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, "Unknown error during mysql_fetch_row or no more rows.");
                    }
                }
                return false;
            }
        }
    }

}  // namespace cpporm_mysql_transport