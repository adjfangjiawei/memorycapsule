#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_statement.h"

namespace cpporm_mysql_transport {

    bool MySqlTransportStatement::prepare() {
        if (m_is_utility_command) {
            m_is_prepared = true;
            return true;
        }

        if (!m_stmt_handle) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle is not initialized for prepare (non-utility command).");
            return false;
        }
        if (m_is_prepared) {
            return true;
        }

        clearError();

        if (mysql_stmt_prepare(m_stmt_handle, m_original_query.c_str(), m_original_query.length()) != 0) {
            setErrorFromStatementHandle("mysql_stmt_prepare failed");
            m_is_prepared = false;
            return false;
        }

        m_is_prepared = true;
        unsigned long param_count_long = mysql_stmt_param_count(m_stmt_handle);
        if (param_count_long > 0) {
            unsigned int param_count = static_cast<unsigned int>(param_count_long);
            m_bind_buffers.assign(param_count, MYSQL_BIND{});
            m_param_data_buffers.assign(param_count, std::vector<unsigned char>());
            m_param_is_null_indicators.assign(param_count, (unsigned char)0);
            m_param_length_indicators.assign(param_count, 0UL);
        } else {
            m_bind_buffers.clear();
            m_param_data_buffers.clear();
            m_param_is_null_indicators.clear();
            m_param_length_indicators.clear();
        }
        return true;
    }

}  // namespace cpporm_mysql_transport