// Transport/MySqlTransport/Source/mysql_transport_connection_utility.cpp
#include <mysql/mysql.h>

#include <iomanip>
#include <limits>  // Required for std::numeric_limits
#include <sstream>
#include <stdexcept>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "mysql_protocol/mysql_type_converter.h"

namespace cpporm_mysql_transport {

    std::string MySqlTransportConnection::escapeString(const std::string& unescaped_str, bool /*treat_backslash_as_meta*/) {
        if (!m_mysql_handle) {
            setErrorManually(MySqlTransportError::Category::InternalError, "MySQL handle not available for escapeString.");
            // 对于常量方法中需要转义的场景，这会是个问题。
            // 如果 formatNativeValueAsLiteral (const) 需要调用此非 const 方法，需要重新考虑。
            // 暂时，我们假设 formatNativeValueAsLiteral 能够通过 const_cast 调用，或者有其他机制。
            // 或者，如果只是用于非 const 场景，则此实现OK。
            return unescaped_str;  // 返回未转义的字符串或抛出异常
        }
        // 清除此特定操作的错误状态不是这里的职责，应由调用者管理。

        std::vector<char> to_buffer(unescaped_str.length() * 2 + 1);
        unsigned long to_length = mysql_real_escape_string(m_mysql_handle, to_buffer.data(), unescaped_str.c_str(), unescaped_str.length());
        return std::string(to_buffer.data(), to_length);
    }

    std::string MySqlTransportConnection::escapeSqlIdentifier(const std::string& identifier) const {
        if (identifier.empty()) {
            return "``";
        }
        std::string escaped_id;
        escaped_id.reserve(identifier.length() + 2 + (identifier.length() / 4));
        escaped_id += '`';
        for (char c : identifier) {
            if (c == '`') {
                escaped_id += "``";
            } else {
                escaped_id += c;
            }
        }
        escaped_id += '`';
        return escaped_id;
    }

    std::string MySqlTransportConnection::formatNativeValueAsLiteral(const mysql_protocol::MySqlNativeValue& nativeValue) const {
        if (nativeValue.is_null()) {
            return "NULL";
        }

        MYSQL* current_handle_for_escaping = const_cast<MYSQL*>(m_mysql_handle);  // Potentially unsafe if m_mysql_handle state is changed by escape

        if (!current_handle_for_escaping && (std::holds_alternative<std::string>(nativeValue.data) || std::holds_alternative<std::vector<unsigned char>>(nativeValue.data))) {
            if (std::holds_alternative<std::string>(nativeValue.data)) {
                // 基本的、不安全的转义，仅用于演示或无连接句柄时的最后手段
                std::string s = std::get<std::string>(nativeValue.data);
                std::string escaped_s;
                escaped_s.push_back('\'');
                for (char ch : s) {
                    if (ch == '\'')
                        escaped_s.append("''");
                    else if (ch == '\\')
                        escaped_s.append("\\\\");
                    else
                        escaped_s.push_back(ch);
                }
                escaped_s.push_back('\'');
                return escaped_s + " /* NO_HANDLE_BASIC_ESCAPE */";
            }
            return "NULL /* FORMATTING_ERROR_NO_HANDLE_FOR_BLOB */";
        }

        if (std::holds_alternative<bool>(nativeValue.data)) {
            return std::get<bool>(nativeValue.data) ? "TRUE" : "FALSE";
        } else if (std::holds_alternative<int8_t>(nativeValue.data)) {
            return std::to_string(std::get<int8_t>(nativeValue.data));
        } else if (std::holds_alternative<uint8_t>(nativeValue.data)) {
            return std::to_string(std::get<uint8_t>(nativeValue.data));
        } else if (std::holds_alternative<int16_t>(nativeValue.data)) {
            return std::to_string(std::get<int16_t>(nativeValue.data));
        } else if (std::holds_alternative<uint16_t>(nativeValue.data)) {
            return std::to_string(std::get<uint16_t>(nativeValue.data));
        } else if (std::holds_alternative<int32_t>(nativeValue.data)) {
            return std::to_string(std::get<int32_t>(nativeValue.data));
        } else if (std::holds_alternative<uint32_t>(nativeValue.data)) {
            return std::to_string(std::get<uint32_t>(nativeValue.data));
        } else if (std::holds_alternative<int64_t>(nativeValue.data)) {
            return std::to_string(std::get<int64_t>(nativeValue.data));
        } else if (std::holds_alternative<uint64_t>(nativeValue.data)) {
            return std::to_string(std::get<uint64_t>(nativeValue.data));
        } else if (std::holds_alternative<float>(nativeValue.data)) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(std::numeric_limits<float>::max_digits10) << std::get<float>(nativeValue.data);
            return oss.str();
        } else if (std::holds_alternative<double>(nativeValue.data)) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(std::numeric_limits<double>::max_digits10) << std::get<double>(nativeValue.data);
            return oss.str();
        } else if (std::holds_alternative<std::string>(nativeValue.data)) {
            const std::string& str_val = std::get<std::string>(nativeValue.data);
            std::vector<char> to_buffer(str_val.length() * 2 + 1);
            unsigned long to_length = mysql_real_escape_string(current_handle_for_escaping, to_buffer.data(), str_val.c_str(), str_val.length());
            return "'" + std::string(to_buffer.data(), to_length) + "'";
        } else if (std::holds_alternative<std::vector<unsigned char>>(nativeValue.data)) {
            const std::vector<unsigned char>& blob_val = std::get<std::vector<unsigned char>>(nativeValue.data);
            if (blob_val.empty()) {
                return "X''";
            }
            std::ostringstream oss;
            oss << "X'";
            for (unsigned char byte : blob_val) {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            oss << "'";
            return oss.str();
        } else if (std::holds_alternative<MYSQL_TIME>(nativeValue.data)) {
            const MYSQL_TIME& mt = std::get<MYSQL_TIME>(nativeValue.data);
            std::ostringstream oss;
            oss << "'";
            bool date_part_exists = (mt.year != 0 || mt.month != 0 || mt.day != 0);
            bool time_part_exists = (mt.hour != 0 || mt.minute != 0 || mt.second != 0 || mt.second_part != 0);

            if (mt.time_type == MYSQL_TIMESTAMP_DATE || (mt.time_type == MYSQL_TIMESTAMP_DATETIME && date_part_exists) || (mt.time_type == MYSQL_TIMESTAMP_NONE && date_part_exists && !time_part_exists)) {
                oss << std::setfill('0') << std::setw(4) << mt.year << "-" << std::setw(2) << mt.month << "-" << std::setw(2) << mt.day;
            } else if (mt.time_type == MYSQL_TIMESTAMP_DATETIME && date_part_exists && time_part_exists) {  // Full DATETIME
                oss << std::setfill('0') << std::setw(4) << mt.year << "-" << std::setw(2) << mt.month << "-" << std::setw(2) << mt.day;
            }

            if (mt.time_type == MYSQL_TIMESTAMP_TIME || (mt.time_type == MYSQL_TIMESTAMP_DATETIME && time_part_exists) || (mt.time_type == MYSQL_TIMESTAMP_NONE && !date_part_exists && time_part_exists)) {
                if (date_part_exists && time_part_exists && mt.time_type == MYSQL_TIMESTAMP_DATETIME) {  // Space for DATETIME
                    oss << " ";
                }
                oss << std::setfill('0') << std::setw(2) << mt.hour << ":" << std::setw(2) << mt.minute << ":" << std::setw(2) << mt.second;
                if (mt.second_part > 0) {
                    std::string sec_part_str = std::to_string(mt.second_part);
                    // Pad to 6 digits for microseconds if it's less, truncate if more.
                    if (sec_part_str.length() < 6) {
                        sec_part_str = std::string(6 - sec_part_str.length(), '0') + sec_part_str;
                    } else if (sec_part_str.length() > 6) {
                        sec_part_str = sec_part_str.substr(0, 6);
                    }
                    // Remove trailing zeros from fractional part
                    size_t last_digit = sec_part_str.find_last_not_of('0');
                    if (last_digit != std::string::npos) {
                        oss << "." << sec_part_str.substr(0, last_digit + 1);
                    }  // else if all zeros, don't append ".000000"
                }
            } else if (mt.time_type == MYSQL_TIMESTAMP_DATETIME && date_part_exists && !time_part_exists) {
                // Date part was written, but no time part for a DATETIME type (e.g. 'YYYY-MM-DD 00:00:00')
                // This case is covered by the first date_part_exists block for DATETIME if time is all zero.
                // If time_type is DATETIME, we generally expect time. If mt.hour etc are 0, they will be formatted as 00.
            }

            oss << "'";
            std::string formatted_literal = oss.str();
            // Handle cases like just time 'HH:MM:SS' or just date 'YYYY-MM-DD'
            // If oss is just "'", it means no part was formatted, which is an error or unhandled MYSQL_TIME state
            if (formatted_literal == "''" && !(date_part_exists || time_part_exists)) {  // If truly empty time struct and nothing written
                if (mt.time_type == MYSQL_TIMESTAMP_ERROR) return "NULL /* MYSQL_TIME ERROR */";
                return "NULL /* INVALID OR UNHANDLED MYSQL_TIME */";
            }
            // If only date or only time was written and it's not ''
            if (formatted_literal.length() > 2) {  // Something was written beyond "''"
                return formatted_literal;
            }
            return "NULL /* INVALID OR UNHANDLED MYSQL_TIME */";  // Fallback
        }

        return "NULL /* UNSUPPORTED NATIVE TYPE FOR LITERAL */";
    }

    bool MySqlTransportConnection::_internalExecuteSimpleQuery(const std::string& query, const std::string& context_message) {
        if (!isConnected()) {
            setErrorManually(MySqlTransportError::Category::ConnectionError, context_message.empty() ? "Not connected to server." : context_message + ": Not connected.");
            return false;
        }
        clearError();
        if (mysql_real_query(m_mysql_handle, query.c_str(), query.length()) != 0) {
            setErrorFromMySqlHandle(m_mysql_handle, context_message.empty() ? "Query failed" : context_message);
            return false;
        }

        int status;
        do {
            MYSQL_RES* result = mysql_store_result(m_mysql_handle);
            if (result) {
                mysql_free_result(result);
            } else {
                if (mysql_field_count(m_mysql_handle) == 0) {
                    // No result set, which is OK for DML, SET, etc.
                } else {
                    setErrorFromMySqlHandle(m_mysql_handle, context_message.empty() ? "Failed to retrieve result after query" : context_message + ": Failed to retrieve result");
                    return false;
                }
            }
            status = mysql_next_result(m_mysql_handle);  // Advances to the next result (if any)
            if (status > 0) {                            // Error
                setErrorFromMySqlHandle(m_mysql_handle, context_message.empty() ? "Error processing multiple results" : context_message + ": Error processing results");
                return false;
            }
        } while (status == 0);  // Loop if status is 0 (more results processed successfully)

        // After loop, status is -1 (no more results) or >0 (error, already handled).
        // If status is -1, it means all results were processed successfully or there were no more results.
        // One final check for errors that might not be caught by mysql_next_result status.
        if (status == -1 && mysql_errno(m_mysql_handle) != 0) {
            setErrorFromMySqlHandle(m_mysql_handle, context_message.empty() ? "Error after processing all results" : context_message + ": Error after processing all results");
            return false;
        }
        return true;
    }

}  // namespace cpporm_mysql_transport