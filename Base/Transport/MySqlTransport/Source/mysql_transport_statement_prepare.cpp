// cpporm_mysql_transport/mysql_transport_statement_prepare.cpp
#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_statement.h"

namespace cpporm_mysql_transport {

    bool MySqlTransportStatement::prepare() {
        if (!m_stmt_handle) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle is not initialized for prepare.");
            return false;
        }
        // If already prepared, subsequent calls could re-prepare.
        // For simplicity, if m_is_prepared is true, we could return true.
        // However, if the query string m_original_query could change and then prepare called again,
        // we must re-prepare. Current design assumes m_original_query is const after construction.
        // If it's desired to re-prepare (e.g. after a reset or if query could change), then:
        // if (m_is_prepared) {
        //    // Optionally, call mysql_stmt_reset(m_stmt_handle) or even mysql_stmt_close and re-init
        //    // For now, let's assume if it's prepared, it's good.
        //    // Or, if we want to allow re-prepare:
        //    // m_is_prepared = false; // Force re-prepare
        // }
        if (m_is_prepared) {  // If truly idempotent
            return true;
        }

        clearError();

        if (mysql_stmt_prepare(m_stmt_handle, m_original_query.c_str(), m_original_query.length()) != 0) {
            setErrorFromMySQL();
            m_is_prepared = false;
            return false;
        }

        m_is_prepared = true;
        unsigned long param_count_long = mysql_stmt_param_count(m_stmt_handle);
        if (param_count_long > 0) {
            unsigned int param_count = static_cast<unsigned int>(param_count_long);
            // Resize and initialize vectors for parameter binding
            m_bind_buffers.assign(param_count, MYSQL_BIND{});                        // Zero-initialize
            m_param_data_buffers.assign(param_count, std::vector<unsigned char>());  // Vector of empty vectors
            m_param_is_null_indicators.assign(param_count, (char)0);                 // 0 for false (not null)
            m_param_length_indicators.assign(param_count, 0UL);
        } else {
            // No parameters, clear any previous bind state
            m_bind_buffers.clear();
            m_param_data_buffers.clear();
            m_param_is_null_indicators.clear();
            m_param_length_indicators.clear();
        }
        return true;
    }

}  // namespace cpporm_mysql_transport