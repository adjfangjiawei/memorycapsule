// cpporm_mysql_transport/mysql_transport_statement_info.cpp
#include "cpporm_mysql_transport/mysql_transport_statement.h"
// No other specific includes needed for these simple getters

namespace cpporm_mysql_transport {

    my_ulonglong MySqlTransportStatement::getAffectedRows() const {
        return m_affected_rows;
    }

    my_ulonglong MySqlTransportStatement::getLastInsertId() const {
        return m_last_insert_id;
    }

    unsigned int MySqlTransportStatement::getWarningCount() const {
        // This returns the warning count from the *connection* after the last statement execution.
        // Not specific to the statement if mysql_stmt_warning_count is unavailable.
        return m_warning_count;
    }

    MySqlTransportError MySqlTransportStatement::getError() const {
        return m_last_error;
    }

}  // namespace cpporm_mysql_transport