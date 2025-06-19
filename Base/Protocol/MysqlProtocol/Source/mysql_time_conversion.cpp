// Source/mysql_protocol/mysql_time_conversion.cpp
#include <cstdio>       // For std::sscanf, snprintf
#include <cstring>      // For std::memset
#include <iomanip>      // For std::setfill, std::setw
#include <sstream>      // For std::ostringstream
#include <string>       // For std::string, std::to_string
#include <string_view>  // For std::string_view

#include "mysql_protocol/mysql_type_converter.h"

// <mysql/mysql.h>, <expected> are included via mysql_type_converter.h

namespace mysql_protocol {

    std::expected<MYSQL_TIME, MySqlProtocolError> parseDateTimeStringToMySqlTime(std::string_view dt_string, enum enum_field_types expected_type) {
        MYSQL_TIME out_mysql_time;
        std::memset(&out_mysql_time, 0, sizeof(MYSQL_TIME));
        out_mysql_time.time_type = MYSQL_TIMESTAMP_ERROR;  // Default to error

        if (dt_string.empty()) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_STRING_PARSE_EMPTY_INPUT, "Input date/time string is empty."));
        }

        std::string s(dt_string);  // sscanf requires a null-terminated string.
        const char* str = s.c_str();

        int year = 0, month = 0, day = 0, hour = 0, minute = 0, sec = 0;  // Renamed to sec to avoid conflict
        unsigned long local_microsecond = 0;                              // Renamed to avoid any potential conflict
        int fields_read = 0;
        bool neg = false;

        if (expected_type == MYSQL_TYPE_TIME || expected_type == MYSQL_TYPE_TIME2) {
            out_mysql_time.time_type = MYSQL_TIMESTAMP_TIME;
            if (str[0] == '-') {
                neg = true;
                str++;
            }
            // Use local_microsecond for sscanf
            fields_read = std::sscanf(str, "%d:%d:%d.%6lu", &hour, &minute, &sec, &local_microsecond);
            if (fields_read < 4) {
                local_microsecond = 0;
                fields_read = std::sscanf(str, "%d:%d:%d", &hour, &minute, &sec);
                if (fields_read < 3) {
                    return std::unexpected(MySqlProtocolError(InternalErrc::TIME_STRING_PARSE_INVALID_FORMAT, "Invalid TIME format: '" + s + "'. Expected H:M:S[.US]."));
                }
            }
            if (hour < 0 || hour > 838 || minute < 0 || minute > 59 || sec < 0 || sec > 59 || local_microsecond > 999999) {
                return std::unexpected(MySqlProtocolError(InternalErrc::TIME_STRING_PARSE_COMPONENT_OUT_OF_RANGE, "Parsed TIME component out of range in '" + s + "'."));
            }
            out_mysql_time.neg = neg;
            out_mysql_time.hour = static_cast<unsigned int>(hour);
            out_mysql_time.minute = static_cast<unsigned int>(minute);
            out_mysql_time.second = static_cast<unsigned int>(sec);  // Assign to MYSQL_TIME::second
            out_mysql_time.second_part = local_microsecond;          // Assign to MYSQL_TIME::second_part
            return out_mysql_time;
        }

        if (expected_type == MYSQL_TYPE_DATE || expected_type == MYSQL_TYPE_NEWDATE || expected_type == MYSQL_TYPE_YEAR) {
            if (expected_type == MYSQL_TYPE_YEAR) {
                fields_read = std::sscanf(str, "%d", &year);
                if (fields_read == 1) {
                    // MySQL YEAR can be '0000' or 1901 to 2155. For parsing, allow wider range.
                    if (year < 0 || year > 9999) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::TIME_STRING_PARSE_COMPONENT_OUT_OF_RANGE, "Parsed YEAR component '" + std::to_string(year) + "' out of range in '" + s + "'."));
                    }
                    out_mysql_time.time_type = MYSQL_TIMESTAMP_DATE;  // Treat YEAR as a special date with only year component
                    out_mysql_time.year = static_cast<unsigned int>(year);
                    out_mysql_time.month = 0;  // For pure YEAR, month/day can be 0
                    out_mysql_time.day = 0;
                    return out_mysql_time;
                }
            }

            out_mysql_time.time_type = MYSQL_TIMESTAMP_DATE;
            fields_read = std::sscanf(str, "%d-%d-%d", &year, &month, &day);
            if (fields_read == 3) {
                if (year < 0 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31) {
                    return std::unexpected(MySqlProtocolError(InternalErrc::TIME_STRING_PARSE_COMPONENT_OUT_OF_RANGE, "Parsed DATE component out of range in '" + s + "'."));
                }
                out_mysql_time.year = static_cast<unsigned int>(year);
                out_mysql_time.month = static_cast<unsigned int>(month);
                out_mysql_time.day = static_cast<unsigned int>(day);
                return out_mysql_time;
            }
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_STRING_PARSE_INVALID_FORMAT, "Invalid DATE/YEAR format: '" + s + "'. Expected YYYY-MM-DD or YYYY."));
        }

        if (expected_type == MYSQL_TYPE_DATETIME || expected_type == MYSQL_TYPE_TIMESTAMP || expected_type == MYSQL_TYPE_DATETIME2 || expected_type == MYSQL_TYPE_TIMESTAMP2) {
            out_mysql_time.time_type = MYSQL_TIMESTAMP_DATETIME;
            // Use local_microsecond for sscanf
            fields_read = std::sscanf(str, "%d-%d-%d %d:%d:%d.%6lu", &year, &month, &day, &hour, &minute, &sec, &local_microsecond);
            if (fields_read < 7) {
                local_microsecond = 0;
                fields_read = std::sscanf(str, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &sec);
                if (fields_read < 6) {
                    return std::unexpected(MySqlProtocolError(InternalErrc::TIME_STRING_PARSE_INVALID_FORMAT, "Invalid DATETIME/TIMESTAMP format: '" + s + "'. Expected YYYY-MM-DD HH:MM:SS[.US]."));
                }
            }
            if (year < 0 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || sec < 0 || sec > 59 || local_microsecond > 999999) {
                return std::unexpected(MySqlProtocolError(InternalErrc::TIME_STRING_PARSE_COMPONENT_OUT_OF_RANGE, "Parsed DATETIME/TIMESTAMP component out of range in '" + s + "'."));
            }
            out_mysql_time.year = static_cast<unsigned int>(year);
            out_mysql_time.month = static_cast<unsigned int>(month);
            out_mysql_time.day = static_cast<unsigned int>(day);
            out_mysql_time.hour = static_cast<unsigned int>(hour);
            out_mysql_time.minute = static_cast<unsigned int>(minute);
            out_mysql_time.second = static_cast<unsigned int>(sec);  // Assign to MYSQL_TIME::second
            out_mysql_time.second_part = local_microsecond;          // Assign to MYSQL_TIME::second_part
            return out_mysql_time;
        }

        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_UNSUPPORTED_TYPE, "Unsupported expected type for date/time string parsing: " + std::to_string(expected_type)));
    }

    std::expected<std::string, MySqlProtocolError> formatMySqlTimeToString(const MYSQL_TIME& mysql_time, enum enum_field_types original_type) {
        std::ostringstream oss;
        oss << std::setfill('0');

        if (original_type == MYSQL_TYPE_YEAR) {
            // For YEAR, only year component is relevant. time_type might be DATE or even ERROR if only year was set.
            oss << std::setw(4) << mysql_time.year;
            if (oss.fail()) return std::unexpected(MySqlProtocolError(InternalErrc::TIME_FORMAT_STREAM_ERROR, "String stream failed formatting YEAR."));
            return oss.str();
        }

        // Check for generally invalid MYSQL_TIME state if not a simple YEAR type
        if (mysql_time.time_type == MYSQL_TIMESTAMP_ERROR) {
            bool is_zero_date = mysql_time.year == 0 && mysql_time.month == 0 && mysql_time.day == 0;
            bool is_zero_time = mysql_time.hour == 0 && mysql_time.minute == 0 && mysql_time.second == 0 && mysql_time.second_part == 0;

            if (original_type == MYSQL_TYPE_DATE || original_type == MYSQL_TYPE_NEWDATE) {
                if (is_zero_date) return "0000-00-00";  // Standard zero date representation
            } else if (original_type == MYSQL_TYPE_TIME || original_type == MYSQL_TYPE_TIME2) {
                if (is_zero_time) return "00:00:00";  // Standard zero time representation
            } else if (original_type == MYSQL_TYPE_DATETIME || original_type == MYSQL_TYPE_TIMESTAMP || original_type == MYSQL_TYPE_DATETIME2 || original_type == MYSQL_TYPE_TIMESTAMP2) {
                if (is_zero_date && is_zero_time) return "0000-00-00 00:00:00";  // Standard zero datetime
            }
            // If not a recognized "zero" pattern with error type, then it's an unformattable error state.
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_FORMAT_INVALID_MYSQL_TIME_STRUCT, "Cannot format MYSQL_TIME with time_type=MYSQL_TIMESTAMP_ERROR and non-standard zero components."));
        }

        if (mysql_time.time_type == MYSQL_TIMESTAMP_DATE || (original_type == MYSQL_TYPE_DATE || original_type == MYSQL_TYPE_NEWDATE)) {  // Ensure correct type check
            if (mysql_time.year == 0 && mysql_time.month == 0 && mysql_time.day == 0) {
                oss << "0000-00-00";
            } else {
                oss << std::setw(4) << mysql_time.year << "-" << std::setw(2) << mysql_time.month << "-" << std::setw(2) << mysql_time.day;
            }
        } else if (mysql_time.time_type == MYSQL_TIMESTAMP_TIME || (original_type == MYSQL_TYPE_TIME || original_type == MYSQL_TYPE_TIME2)) {
            if (mysql_time.neg) oss << "-";
            oss << std::setw(2) << mysql_time.hour << ":" << std::setw(2) << mysql_time.minute << ":" << std::setw(2) << mysql_time.second;
            if (mysql_time.second_part > 0) {
                char micro_buf[7];
                snprintf(micro_buf, sizeof(micro_buf), "%06lu", mysql_time.second_part % 1000000UL);
                oss << "." << micro_buf;
            }
        } else if (mysql_time.time_type == MYSQL_TIMESTAMP_DATETIME || (original_type == MYSQL_TYPE_DATETIME || original_type == MYSQL_TYPE_TIMESTAMP || original_type == MYSQL_TYPE_DATETIME2 || original_type == MYSQL_TYPE_TIMESTAMP2)) {
            if (mysql_time.year == 0 && mysql_time.month == 0 && mysql_time.day == 0 && mysql_time.hour == 0 && mysql_time.minute == 0 && mysql_time.second == 0 && mysql_time.second_part == 0) {
                oss << "0000-00-00 00:00:00";
            } else {
                oss << std::setw(4) << mysql_time.year << "-" << std::setw(2) << mysql_time.month << "-" << std::setw(2) << mysql_time.day << " " << std::setw(2) << mysql_time.hour << ":" << std::setw(2) << mysql_time.minute << ":" << std::setw(2) << mysql_time.second;
                if (mysql_time.second_part > 0) {
                    char micro_buf[7];
                    snprintf(micro_buf, sizeof(micro_buf), "%06lu", mysql_time.second_part % 1000000UL);
                    oss << "." << micro_buf;
                }
            }
        } else {
            // This case might be hit if mysql_time.time_type is valid (e.g. MYSQL_TIMESTAMP_NONE) but original_type is not directly handled above.
            // Or if mysql_time has a valid type but components that don't make sense for original_type.
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_FORMAT_INVALID_MYSQL_TIME_STRUCT, "Unhandled MYSQL_TIME.time_type (" + std::to_string(mysql_time.time_type) + ") or original_type (" + std::to_string(original_type) + ") combination for formatting."));
        }

        if (oss.fail()) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_FORMAT_STREAM_ERROR, "String stream failed during MYSQL_TIME formatting."));
        }
        return oss.str();
    }

}  // namespace mysql_protocol