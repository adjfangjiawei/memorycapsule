// cpporm_mysql_transport/mysql_transport_types.cpp
#include "cpporm_mysql_transport/mysql_transport_types.h"

#include <mysql/mysql.h>
#include <mysql/mysql_time.h>  // For MYSQL_TIMESTAMP_TYPE enum values, included via mysql.h

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "mysql_protocol/mysql_type_converter.h"

namespace cpporm_mysql_transport {

    // --- MySqlTransportError ---
    MySqlTransportError::MySqlTransportError(Category cat, std::string msg, int mysql_err, const char* mysql_state, const char* mysql_msg_str, unsigned int proto_errc, std::string query)
        : category(cat), native_mysql_errno(mysql_err), protocol_internal_errc(proto_errc), message(std::move(msg)), failed_query(std::move(query)) {
        if (mysql_state) native_mysql_sqlstate = mysql_state;
        if (mysql_msg_str) native_mysql_error_msg = mysql_msg_str;
    }

    std::string MySqlTransportError::toString() const {
        std::string full_msg = "MySqlTransportError: [Category: ";
        switch (category) {
            case Category::NoError:
                full_msg += "NoError";
                break;
            case Category::ConnectionError:
                full_msg += "ConnectionError";
                break;
            case Category::QueryError:
                full_msg += "QueryError";
                break;
            case Category::DataError:
                full_msg += "DataError";
                break;
            case Category::ResourceError:
                full_msg += "ResourceError";
                break;
            case Category::TransactionError:
                full_msg += "TransactionError";
                break;
            case Category::InternalError:
                full_msg += "InternalError";
                break;
            case Category::ProtocolError:
                full_msg += "ProtocolError";
                break;
            case Category::ApiUsageError:
                full_msg += "ApiUsageError";
                break;
            default:
                full_msg += "Unknown (" + std::to_string(static_cast<int>(category)) + ")";
                break;
        }
        full_msg += "] Message: " + message;
        if (native_mysql_errno != 0) {
            full_msg += " | MySQL Errno: " + std::to_string(native_mysql_errno);
        }
        if (!native_mysql_sqlstate.empty()) {
            full_msg += " | MySQL SQLSTATE: " + native_mysql_sqlstate;
        }
        if (!native_mysql_error_msg.empty() && native_mysql_error_msg != message) {
            full_msg += " | MySQL Error Msg: " + native_mysql_error_msg;
        }
        if (protocol_internal_errc != 0) {
            full_msg += " | Protocol InternalErrc: " + std::to_string(protocol_internal_errc);
        }
        if (!failed_query.empty()) {
            full_msg += " | Failed Query: " + failed_query;
        }
        return full_msg;
    }

    // --- MySqlTransportFieldMeta ---
    bool MySqlTransportFieldMeta::isGenerallyNumeric() const {
        switch (native_type_id) {
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_NEWDECIMAL:
            case MYSQL_TYPE_TINY:
            case MYSQL_TYPE_SHORT:
            case MYSQL_TYPE_LONG:
            case MYSQL_TYPE_FLOAT:
            case MYSQL_TYPE_DOUBLE:
            case MYSQL_TYPE_LONGLONG:
            case MYSQL_TYPE_INT24:
            case MYSQL_TYPE_BIT:
            case MYSQL_TYPE_YEAR:
                return true;
            default:
                return false;
        }
    }

    bool MySqlTransportFieldMeta::isGenerallyString() const {
        switch (native_type_id) {
            case MYSQL_TYPE_VARCHAR:
            case MYSQL_TYPE_VAR_STRING:
            case MYSQL_TYPE_STRING:
            case MYSQL_TYPE_TINY_BLOB:
            case MYSQL_TYPE_MEDIUM_BLOB:
            case MYSQL_TYPE_LONG_BLOB:
            case MYSQL_TYPE_BLOB:
            case MYSQL_TYPE_JSON:
            case MYSQL_TYPE_ENUM:
            case MYSQL_TYPE_SET:
            case MYSQL_TYPE_GEOMETRY:
                return true;
            default:
                return false;
        }
    }

    bool MySqlTransportFieldMeta::isGenerallyDateTime() const {
        switch (native_type_id) {
            case MYSQL_TYPE_TIMESTAMP:
            case MYSQL_TYPE_DATE:
            case MYSQL_TYPE_TIME:
            case MYSQL_TYPE_DATETIME:
            case MYSQL_TYPE_NEWDATE:
            case MYSQL_TYPE_YEAR:
                return true;
            default:
                return false;
        }
    }

    // --- MySqlTransportBindParam Constructors ---
    MySqlTransportBindParam::MySqlTransportBindParam() {
        value.data = std::monostate{};
        value.original_mysql_type = MYSQL_TYPE_NULL;
    }

    MySqlTransportBindParam::MySqlTransportBindParam(const mysql_protocol::MySqlNativeValue& v) : value(v) {
    }
    MySqlTransportBindParam::MySqlTransportBindParam(mysql_protocol::MySqlNativeValue&& v) : value(std::move(v)) {
    }

    MySqlTransportBindParam::MySqlTransportBindParam(std::nullptr_t) {
        value.data = std::monostate{};
        value.original_mysql_type = MYSQL_TYPE_NULL;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(bool val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_TINY;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(int8_t val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_TINY;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(uint8_t val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_TINY;
        value.original_mysql_flags |= UNSIGNED_FLAG;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(int16_t val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_SHORT;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(uint16_t val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_SHORT;
        value.original_mysql_flags |= UNSIGNED_FLAG;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(int32_t val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_LONG;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(uint32_t val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_LONG;
        value.original_mysql_flags |= UNSIGNED_FLAG;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(int64_t val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_LONGLONG;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(uint64_t val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_LONGLONG;
        value.original_mysql_flags |= UNSIGNED_FLAG;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(float val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_FLOAT;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(double val) {
        value.data = val;
        value.original_mysql_type = MYSQL_TYPE_DOUBLE;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(const char* cval) {
        if (cval) {
            value.data = std::string(cval);
            value.original_mysql_type = MYSQL_TYPE_STRING;
        } else {
            value.data = std::monostate{};
            value.original_mysql_type = MYSQL_TYPE_NULL;
        }
    }
    MySqlTransportBindParam::MySqlTransportBindParam(const std::string& val_str) {
        value.data = val_str;
        value.original_mysql_type = MYSQL_TYPE_STRING;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(std::string&& val_str) {
        value.data = std::move(val_str);
        value.original_mysql_type = MYSQL_TYPE_STRING;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(std::string_view val_sv) {
        value.data = std::string(val_sv);
        value.original_mysql_type = MYSQL_TYPE_STRING;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(const std::vector<unsigned char>& val_blob) {
        value.data = val_blob;
        value.original_mysql_type = MYSQL_TYPE_BLOB;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(std::vector<unsigned char>&& val_blob) {
        value.data = std::move(val_blob);
        value.original_mysql_type = MYSQL_TYPE_BLOB;
    }
    MySqlTransportBindParam::MySqlTransportBindParam(const MYSQL_TIME& val_time) {
        value.data = val_time;
        // MYSQL_TIMESTAMP_YEAR is not in the provided mysql_time.h
        // A MYSQL_TIME struct representing a YEAR type would have time_type = MYSQL_TIMESTAMP_DATE
        // and month=0, day=0 (or month=1, day=1, depending on how it's set for YEAR).
        // For binding, we'd likely bind it as MYSQL_TYPE_YEAR if it's just an integer year,
        // or MYSQL_TYPE_DATE if it's a full MYSQL_TIME struct.
        // Since this constructor takes MYSQL_TIME, we'll map based on its time_type.
        switch (val_time.time_type) {
            case MYSQL_TIMESTAMP_DATE:
                value.original_mysql_type = MYSQL_TYPE_DATE;
                // If val_time.month == 0 && val_time.day == 0, it might represent a YEAR,
                // but its native C API type would still be MYSQL_TIMESTAMP_DATE.
                // For binding, if the target column is YEAR, then MYSQL_TYPE_YEAR might be better.
                // However, this constructor doesn't know the target column type.
                break;
            case MYSQL_TIMESTAMP_DATETIME:
            case MYSQL_TIMESTAMP_DATETIME_TZ:
                value.original_mysql_type = MYSQL_TYPE_DATETIME;
                break;
            case MYSQL_TIMESTAMP_TIME:
                value.original_mysql_type = MYSQL_TYPE_TIME;
                break;
            // No MYSQL_TIMESTAMP_YEAR case
            case MYSQL_TIMESTAMP_NONE:
            case MYSQL_TIMESTAMP_ERROR:
            default:
                if (val_time.time_type == MYSQL_TIMESTAMP_ERROR || (val_time.time_type == MYSQL_TIMESTAMP_NONE && val_time.year == 0 && val_time.month == 0 && val_time.day == 0 && val_time.hour == 0 && val_time.minute == 0 && val_time.second == 0 && val_time.second_part == 0)) {
                    value.data = std::monostate{};
                    value.original_mysql_type = MYSQL_TYPE_NULL;
                } else {
                    // For MYSQL_TIMESTAMP_NONE with some data, or unknown time_type,
                    // DATETIME is a general fallback if it has date and time parts.
                    // If only date parts, DATE; if only time, TIME.
                    // This is a simplification; a more robust mapping might be needed.
                    if (val_time.hour == 0 && val_time.minute == 0 && val_time.second == 0 && val_time.second_part == 0) {
                        value.original_mysql_type = MYSQL_TYPE_DATE;  // If only date components are significant
                    } else if (val_time.year == 0 && val_time.month == 0 && val_time.day == 0) {
                        value.original_mysql_type = MYSQL_TYPE_TIME;  // If only time components are significant
                    } else {
                        value.original_mysql_type = MYSQL_TYPE_DATETIME;
                    }
                }
                break;
        }
    }

    MySqlTransportBindParam::MySqlTransportBindParam(const std::chrono::system_clock::time_point& tp) {
        auto expected_mysql_time = mysql_protocol::systemClockTimePointToMySqlTime(tp, MYSQL_TYPE_DATETIME);
        if (expected_mysql_time) {
            value.data = expected_mysql_time.value();
            value.original_mysql_type = MYSQL_TYPE_DATETIME;
        } else {
            value.data = std::monostate{};
            value.original_mysql_type = MYSQL_TYPE_NULL;
        }
    }
    MySqlTransportBindParam::MySqlTransportBindParam(const std::chrono::year_month_day& ymd) {
        auto expected_mysql_time = mysql_protocol::yearMonthDayToMySqlDate(ymd);
        if (expected_mysql_time) {
            value.data = expected_mysql_time.value();
            value.original_mysql_type = MYSQL_TYPE_DATE;
        } else {
            value.data = std::monostate{};
            value.original_mysql_type = MYSQL_TYPE_NULL;
        }
    }
    MySqlTransportBindParam::MySqlTransportBindParam(const std::chrono::microseconds& duration) {
        auto expected_mysql_time = mysql_protocol::durationToMySqlTime(duration);
        if (expected_mysql_time) {
            value.data = expected_mysql_time.value();
            value.original_mysql_type = MYSQL_TYPE_TIME;
        } else {
            value.data = std::monostate{};
            value.original_mysql_type = MYSQL_TYPE_NULL;
        }
    }

}  // namespace cpporm_mysql_transport