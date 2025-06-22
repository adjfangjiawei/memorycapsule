// SqlDriver/Source/mysql/mysql_error_converter.cpp
#include <mysql/errmsg.h>  // For CR_* client error codes from the version you provided
#include <mysql/mysql.h>   // For CR_MIN_ERROR, CR_MAX_ERROR (used for range check only)

#include "sqldriver/mysql/mysql_driver_helper.h"

// NO server-side ER_* numeric values will be used for classification.
// Classification will rely on MySqlTransportError::category and SQLSTATE.

namespace cpporm_sqldriver {
    namespace mysql_helper {

        SqlError transportErrorToSqlError(const ::cpporm_mysql_transport::MySqlTransportError& transportError) {
            ErrorCategory category = ErrorCategory::Unknown;
            std::string db_text = transportError.native_mysql_error_msg;
            if (db_text.empty()) {
                db_text = transportError.message;
            }
            std::string driver_text = transportError.message;
            int native_err_no = transportError.native_mysql_errno;  // Primarily for debugging or CR_* specifics
            const std::string sqlstate = transportError.native_mysql_sqlstate;

            // 1. Primary mapping based on MySqlTransportError::Category
            switch (transportError.category) {
                case ::cpporm_mysql_transport::MySqlTransportError::Category::NoError:
                    category = ErrorCategory::NoError;
                    break;

                case ::cpporm_mysql_transport::MySqlTransportError::Category::ConnectionError:
                    category = ErrorCategory::Connectivity;  // Default for this transport category
                    // Refine based on SQLSTATE for common connection issues
                    if (sqlstate == "08001" || sqlstate == "08004" || sqlstate == "08S01") {
                        category = ErrorCategory::Connectivity;
                    } else if (sqlstate == "28000") {  // Invalid authorization specification
                        category = ErrorCategory::Permissions;
                    } else {
                        // Check specific CR_* codes that clearly indicate a type of connection failure
                        // Only use CR_* macros that are confirmed to be in your errmsg.h
                        switch (native_err_no) {
                            case CR_CONN_HOST_ERROR:
                            case CR_CONNECTION_ERROR:
                            case CR_SERVER_GONE_ERROR:
                            case CR_SERVER_LOST:
                            case CR_SERVER_LOST_EXTENDED:
                            case CR_SSL_CONNECTION_ERROR:
                            case CR_CONN_UNKNOW_PROTOCOL:  // Sic, from your errmsg.h
                                category = ErrorCategory::Connectivity;
                                break;
                            case CR_AUTH_PLUGIN_CANNOT_LOAD:
                            case CR_AUTH_PLUGIN_ERR:
                                category = ErrorCategory::Permissions;
                                break;
                                // Other CR_* connection errors default to Connectivity
                        }
                    }
                    break;

                case ::cpporm_mysql_transport::MySqlTransportError::Category::QueryError:
                    // For QueryError, SQLSTATE is the primary classifier.
                    if (sqlstate.empty() || sqlstate == "00000") {
                        category = (native_err_no == 0) ? ErrorCategory::NoError : ErrorCategory::DatabaseInternal;
                    } else if (sqlstate.rfind("01", 0) == 0) {  // Warning
                        // Assuming transport's QueryError means it's an actual error for SqlError context
                        category = ErrorCategory::DataRelated;  // Warnings often related to data issues
                    } else if (sqlstate.rfind("21", 0) == 0) {  // Cardinality violation
                        category = ErrorCategory::DataRelated;
                    } else if (sqlstate.rfind("22", 0) == 0) {  // Data exception
                        category = ErrorCategory::DataRelated;
                    } else if (sqlstate.rfind("23", 0) == 0) {  // Integrity constraint violation
                        category = ErrorCategory::Constraint;
                    } else if (sqlstate.rfind("28", 0) == 0) {  // Invalid authorization specification
                        category = ErrorCategory::Permissions;
                    } else if (sqlstate.rfind("3D", 0) == 0 || sqlstate.rfind("3F", 0) == 0) {  // Invalid catalog/schema name
                        category = ErrorCategory::Syntax;
                    } else if (sqlstate.rfind("40", 0) == 0) {  // Transaction rollback (e.g., deadlock, serialization failure)
                        category = ErrorCategory::Transaction;
                        // SQLSTATE 40001 is serialization_failure (includes deadlock)
                        // SQLSTATE 40000 is transaction_rollback
                        // SQLSTATE 40002 is transaction_integrity_constraint_violation
                        // No need to check native_err_no for deadlock/lock_timeout if SQLSTATE is 40xxx
                    } else if (sqlstate.rfind("42", 0) == 0) {  // Syntax error or access rule violation
                        // SQLSTATE class 42 covers a broad range.
                        // "42000" can be syntax error OR access rule violation.
                        // "42S01" table already exists, "42S02" table not found, "42S22" column not found.
                        // Defaulting to Syntax as it covers structural/naming issues.
                        // If "Access denied" type messages are present in db_text for 42000, it's a hint,
                        // but string parsing is fragile. The transport layer's initial category is key.
                        category = ErrorCategory::Syntax;
                    } else if (sqlstate == "HY000") {  // General error
                        // This is very generic. MySqlTransportError::category was QueryError.
                        // Only rely on specific CR_* codes if they indicate API misuse.
                        if (native_err_no == CR_COMMANDS_OUT_OF_SYNC) {
                            category = ErrorCategory::DriverInternal;  // Client API misuse
                        } else {
                            // If it's a server-originated error (native_err_no < CR_MIN_ERROR typically for ER_*)
                            // and SQLSTATE is HY000, it's a general server-side failure.
                            if (native_err_no > 0 && native_err_no < CR_MIN_ERROR) {
                                category = ErrorCategory::DatabaseInternal;
                            } else {
                                category = ErrorCategory::Unknown;  // Could be an unmapped client error or other
                            }
                        }
                    } else {
                        // Unmapped SQLSTATE that is not a warning, and not HY000.
                        // Could be a more specific server error class.
                        category = ErrorCategory::DatabaseInternal;  // Default for unrecognised SQLSTATEs indicating server issues
                    }
                    break;

                case ::cpporm_mysql_transport::MySqlTransportError::Category::DataError:
                    category = ErrorCategory::DataRelated;
                    if (native_err_no == CR_DATA_TRUNCATED) { /* Confirms DataRelated */
                    }
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
                    // These are issues with how the transport layer's API was called.
                    // Specific CR_* codes from your errmsg.h for API misuse:
                    switch (native_err_no) {
                        case CR_NULL_POINTER:
                        case CR_NO_PREPARE_STMT:
                        case CR_PARAMS_NOT_BOUND:
                        case CR_NO_PARAMETERS_EXISTS:
                        case CR_INVALID_PARAMETER_NO:
                        case CR_INVALID_BUFFER_USE:
                        case CR_UNSUPPORTED_PARAM_TYPE:
                        case CR_NO_STMT_METADATA:
                        case CR_STMT_CLOSED:
                        case CR_INVALID_CONN_HANDLE:
                        case CR_FETCH_CANCELED:  // Can be API induced or server
                        case CR_NOT_IMPLEMENTED:
                            category = ErrorCategory::DriverInternal;  // Clear API misuse by client of transport/driver
                            break;
                        default:
                            category = ErrorCategory::DriverInternal;  // Generic API usage error
                            break;
                    }
                    driver_text = "Transport API Usage: " + transportError.message;
                    break;
                default:
                    category = ErrorCategory::Unknown;
            }

            // Final consistency check: if transport error indicates success, SqlError should also be NoError,
            // unless a DriverInternal error (like ProtocolError) occurred despite underlying success.
            if (transportError.isOk() && category != ErrorCategory::NoError) {
                if (!(category == ErrorCategory::DriverInternal && (transportError.category == ::cpporm_mysql_transport::MySqlTransportError::Category::ProtocolError || transportError.category == ::cpporm_mysql_transport::MySqlTransportError::Category::InternalError ||
                                                                    transportError.category == ::cpporm_mysql_transport::MySqlTransportError::Category::ApiUsageError))) {
                    // If transport said OK, but we derived an error (and it's not an overriding DriverInternal issue),
                    // then force NoError. This prioritizes the transport layer's success signal.
                    category = ErrorCategory::NoError;
                }
            }

            return SqlError(category,
                            db_text,
                            driver_text,
                            transportError.native_mysql_sqlstate,
                            native_err_no,  // Still pass for logging/debugging
                            transportError.failed_query);
        }

        SqlError protocolErrorToSqlError(const mysql_protocol::MySqlProtocolError& protocolError, const std::string& context_message) {
            ErrorCategory category = ErrorCategory::DriverInternal;  // Default for protocol issues
            std::string combined_message = context_message;
            if (!combined_message.empty() && !protocolError.error_message.empty()) {
                combined_message += " - ";
            }
            combined_message += protocolError.error_message;

            unsigned int pe_code = protocolError.error_code;  // This is MySqlProtocol::InternalErrc

            if (pe_code == mysql_protocol::InternalErrc::SUCCESS) {
                category = ErrorCategory::NoError;
            } else if (pe_code >= mysql_protocol::InternalErrc::CONVERSION_INVALID_INPUT_ARGUMENT && pe_code <= mysql_protocol::InternalErrc::CONVERSION_TYPE_MISMATCH_ACCESS) {
                category = ErrorCategory::DataRelated;
            } else if (pe_code >= mysql_protocol::InternalErrc::TIME_STRING_PARSE_EMPTY_INPUT && pe_code <= mysql_protocol::InternalErrc::TIME_CHRONO_CONVERSION_UNSUPPORTED_TYPE) {
                category = ErrorCategory::DataRelated;
            } else if (pe_code == mysql_protocol::InternalErrc::NATIVE_VALUE_TO_STRING_ERROR) {
                category = ErrorCategory::DataRelated;
            }
            // Other InternalErrc codes like BIND_SETUP, LOGIC_ERROR_INVALID_STATE, UNKNOWN_ERROR
            // will correctly default to ErrorCategory::DriverInternal.

            return SqlError(category,
                            protocolError.error_message,
                            combined_message,
                            std::string(protocolError.sql_state),  // Protocol's SQLSTATE
                            static_cast<int>(pe_code),             // Use protocol's internal code
                            "");
        }

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver