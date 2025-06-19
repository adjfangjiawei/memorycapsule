// Source/mysql_protocol/mysql_error_reporting.cpp
#include <cstring>  // For std::strncpy, std::strcmp
#include <string>   // For std::string

#include "mysql_protocol/mysql_type_converter.h"

// <mysql/mysql.h>, <expected> are included via mysql_type_converter.h

namespace mysql_protocol {

    MySqlProtocolError getMySqlHandleError(MYSQL* handle) {
        if (!handle) {
            return MySqlProtocolError(InternalErrc::CONVERSION_INVALID_INPUT_ARGUMENT, "MYSQL handle is null.");
        }
        unsigned int err_no = mysql_errno(handle);
        const char* sql_state = mysql_sqlstate(handle);
        std::string err_msg = mysql_error(handle);

        if (err_no == 0) {
            // Even if err_no is 0, check sql_state. "00000" is success.
            // If sql_state is not "00000" but err_no is 0, it's unusual but treat as success from error code perspective.
            // MySqlProtocolError() default constructor handles success.
            // We can make the success message more explicit if mysql_error also returns something.
            MySqlProtocolError success_err;  // Defaults to InternalErrc::SUCCESS and "00000"
            if (!err_msg.empty() && err_msg != "NULL" && err_msg.find(" অভ") == std::string::npos /* common non-error msg */) {
                // Sometimes mysql_error returns non-empty for success, e.g. "Query OK..."
                // For consistency, let's keep the default "Success" or be more specific.
                success_err.error_message = "Success (MySQL: " + err_msg + ")";
            } else {
                success_err.error_message = "Success";
            }
            return success_err;
        }
        // Error from MySQL C API
        return MySqlProtocolError(err_no, sql_state, err_msg);
    }

    MySqlProtocolError getMySqlStmtError(MYSQL_STMT* stmt_handle) {
        if (!stmt_handle) {
            return MySqlProtocolError(InternalErrc::CONVERSION_INVALID_INPUT_ARGUMENT, "MYSQL_STMT handle is null.");
        }
        unsigned int err_no = mysql_stmt_errno(stmt_handle);
        const char* sql_state = mysql_stmt_sqlstate(stmt_handle);
        std::string err_msg = mysql_stmt_error(stmt_handle);

        if (err_no == 0) {
            MySqlProtocolError success_err;
            if (!err_msg.empty() && err_msg != "NULL" && err_msg.find(" অভ") == std::string::npos) {
                success_err.error_message = "Success (MySQL STMT: " + err_msg + ")";
            } else {
                success_err.error_message = "Success";
            }
            return success_err;
        }
        // Error from MySQL C API
        return MySqlProtocolError(err_no, sql_state, err_msg);
    }

}  // namespace mysql_protocol