// SqlDriver/Source/mysql/mysql_error_converter.cpp
#include <mysql/errmsg.h>  // Explicitly include for CR_* client error codes
#include <mysql/mysql.h>   // General MySQL header, should pull in errmsg.h for CR_*

#include "sqldriver/mysql/mysql_driver_helper.h"

// Server error codes (ER_*) are problematic to include directly sometimes.
// We will use their numeric values.
// Common server error codes (for reference, not as macros here):
// ER_ACCESS_DENIED_ERROR           1045
// ER_DBACCESS_DENIED_ERROR         1044
// ER_PARSE_ERROR                   1064
// ER_SYNTAX_ERROR                  1149 (less common, 1064 is typical)
// ER_DUP_ENTRY                     1062
// ER_NO_REFERENCED_ROW_2           1452
// ER_ROW_IS_REFERENCED_2           1451
// ER_TABLEACCESS_DENIED_ERROR      1142
// ER_COLUMNACCESS_DENIED_ERROR     1143
// ER_LOCK_WAIT_TIMEOUT             1205
// ER_LOCK_DEADLOCK                 1213
// ER_DATA_TOO_LONG                 1406
// ER_TRUNCATED_WRONG_VALUE_FOR_FIELD 1366
// ER_TRUNCATED_WRONG_VALUE         1292
// ER_BAD_NULL_ERROR                1048
// ER_BAD_FIELD_ERROR               1054 (Unknown column)
// ER_NO_SUCH_TABLE                 1146

namespace cpporm_sqldriver {
    namespace mysql_helper {

        SqlError transportErrorToSqlError(const ::cpporm_mysql_transport::MySqlTransportError& transportError) {
            ErrorCategory category = ErrorCategory::Unknown;
            std::string db_text = transportError.native_mysql_error_msg;
            if (db_text.empty()) {
                db_text = transportError.message;
            }
            std::string driver_text = transportError.message;

            switch (transportError.category) {
                case ::cpporm_mysql_transport::MySqlTransportError::Category::NoError:
                    category = ErrorCategory::NoError;
                    break;
                case ::cpporm_mysql_transport::MySqlTransportError::Category::ConnectionError:
                    category = ErrorCategory::Connectivity;
                    if (transportError.native_mysql_errno == CR_CONN_HOST_ERROR || transportError.native_mysql_errno == CR_CONNECTION_ERROR) {
                        // category remains Connectivity
                    } else if (transportError.native_mysql_errno == 1045) {  // ER_ACCESS_DENIED_ERROR
                        category = ErrorCategory::Permissions;
                    } else if (transportError.native_mysql_errno == CR_SERVER_GONE_ERROR || transportError.native_mysql_errno == CR_SERVER_LOST) {
                        category = ErrorCategory::Connectivity;
                    }
                    break;
                case ::cpporm_mysql_transport::MySqlTransportError::Category::QueryError:
                    switch (transportError.native_mysql_errno) {
                        case 1064:  // ER_PARSE_ERROR
                        case 1149:  // ER_SYNTAX_ERROR (alternative, less common)
                            category = ErrorCategory::Syntax;
                            break;
                        case 1062:  // ER_DUP_ENTRY
                        case 1451:  // ER_ROW_IS_REFERENCED_2
                        case 1452:  // ER_NO_REFERENCED_ROW_2
                        case 1216:  // ER_NO_REFERENCED_ROW (older)
                        case 1217:  // ER_ROW_IS_REFERENCED (older)
                        case 1048:  // ER_BAD_NULL_ERROR (NOT NULL constraint)
                            category = ErrorCategory::Constraint;
                            break;
                        case 1142:  // ER_TABLEACCESS_DENIED_ERROR
                        case 1143:  // ER_COLUMNACCESS_DENIED_ERROR
                        case 1044:  // ER_DBACCESS_DENIED_ERROR
                            category = ErrorCategory::Permissions;
                            break;
                        case 1205:  // ER_LOCK_WAIT_TIMEOUT
                        case 1213:  // ER_LOCK_DEADLOCK
                            category = ErrorCategory::Resource;
                            break;
                        case 1406:  // ER_DATA_TOO_LONG
                        case 1366:  // ER_TRUNCATED_WRONG_VALUE_FOR_FIELD
                        case 1292:  // ER_TRUNCATED_WRONG_VALUE
                            category = ErrorCategory::DataRelated;
                            break;
                        case 1054:  // ER_BAD_FIELD_ERROR
                            category = ErrorCategory::Syntax;
                            break;
                        case 1146:  // ER_NO_SUCH_TABLE
                            category = ErrorCategory::Syntax;
                            break;
                        default:
                            if (transportError.native_mysql_errno > 0 && transportError.native_mysql_errno < CR_MIN_ERROR) {  // Server errors are typically < CR_MIN_ERROR
                                category = ErrorCategory::DatabaseInternal;
                            } else if (transportError.native_mysql_errno >= CR_MIN_ERROR) {  // Client errors
                                category = ErrorCategory::Connectivity;                      // Or DriverInternal for some client errors
                            } else {
                                category = ErrorCategory::Syntax;  // Default for general query errors
                            }
                    }
                    break;
                case ::cpporm_mysql_transport::MySqlTransportError::Category::DataError:
                    category = ErrorCategory::DataRelated;
                    break;
                case ::cpporm_mysql_transport::MySqlTransportError::Category::ResourceError:
                    category = ErrorCategory::Resource;
                    break;
                case ::cpporm_mysql_transport::MySqlTransportError::Category::TransactionError:
                    category = ErrorCategory::Transaction;
                    break;
                case ::cpporm_mysql_transport::MySqlTransportError::Category::ProtocolError:
                    category = ErrorCategory::DriverInternal;
                    driver_text = "Protocol Layer: " + transportError.message;
                    break;
                case ::cpporm_mysql_transport::MySqlTransportError::Category::InternalError:
                    category = ErrorCategory::DriverInternal;
                    driver_text = "Transport Internal: " + transportError.message;
                    break;
                case ::cpporm_mysql_transport::MySqlTransportError::Category::ApiUsageError:
                    category = ErrorCategory::DriverInternal;
                    driver_text = "Transport API Usage: " + transportError.message;
                    break;
                default:
                    category = ErrorCategory::Unknown;
            }

            return SqlError(category, db_text, driver_text, transportError.native_mysql_sqlstate, transportError.native_mysql_errno, transportError.failed_query);
        }

        SqlError protocolErrorToSqlError(const mysql_protocol::MySqlProtocolError& protocolError, const std::string& context_message) {
            ErrorCategory category = ErrorCategory::DriverInternal;
            std::string combined_message = context_message;
            if (!combined_message.empty() && !protocolError.error_message.empty()) {
                combined_message += " - ";
            }
            combined_message += protocolError.error_message;

            unsigned int pe_code = protocolError.error_code;

            if (pe_code == mysql_protocol::InternalErrc::SUCCESS) {
                category = ErrorCategory::NoError;
            } else if (pe_code >= mysql_protocol::InternalErrc::CONVERSION_INVALID_INPUT_ARGUMENT && pe_code <= mysql_protocol::InternalErrc::CONVERSION_TYPE_MISMATCH_ACCESS) {
                category = ErrorCategory::DataRelated;
            } else if (pe_code >= mysql_protocol::InternalErrc::TIME_STRING_PARSE_EMPTY_INPUT && pe_code <= mysql_protocol::InternalErrc::TIME_CHRONO_CONVERSION_UNSUPPORTED_TYPE) {
                category = ErrorCategory::DataRelated;
            } else if (pe_code >= mysql_protocol::InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT && pe_code < mysql_protocol::InternalErrc::NATIVE_VALUE_TO_STRING_ERROR) {
                category = ErrorCategory::DriverInternal;
            } else if (pe_code == mysql_protocol::InternalErrc::NATIVE_VALUE_TO_STRING_ERROR) {
                category = ErrorCategory::DataRelated;
            } else if (pe_code == mysql_protocol::InternalErrc::LOGIC_ERROR_INVALID_STATE) {
                category = ErrorCategory::DriverInternal;
            }
            // else other InternalErrc can be mapped as needed

            return SqlError(category, protocolError.error_message, combined_message, std::string(protocolError.sql_state), static_cast<int>(pe_code), "");
        }

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver