// Include/mysql_protocol/mysql_type_converter.h
#pragma once

#include <mysql/mysql.h>

#include <chrono>    // For std::chrono types
#include <cstring>   // For std::strcmp, std::strncpy
#include <expected>  // C++23, for std::expected
#include <limits>    // For std::numeric_limits
#include <optional>  // For std::optional in get_if
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mysql_protocol {

    // 自定义内部错误码，用于 MySqlProtocolError::error_code
    namespace InternalErrc {
        constexpr unsigned int SUCCESS = 0;  // 成功

        // 类型转换错误码 (10000 - 10099)
        constexpr unsigned int CONVERSION_INVALID_INPUT_ARGUMENT = 10000;
        constexpr unsigned int CONVERSION_INVALID_FORMAT = 10001;
        constexpr unsigned int CONVERSION_VALUE_OUT_OF_RANGE = 10002;
        constexpr unsigned int CONVERSION_UNSUPPORTED_TYPE = 10003;
        constexpr unsigned int CONVERSION_NULL_INPUT_UNEXPECTED = 10004;
        constexpr unsigned int CONVERSION_TYPE_MISMATCH_ACCESS = 10005;

        // MYSQL_TIME 解析/格式化/转换错误码 (10100 - 10199)
        constexpr unsigned int TIME_STRING_PARSE_EMPTY_INPUT = 10101;
        constexpr unsigned int TIME_STRING_PARSE_INVALID_FORMAT = 10102;
        constexpr unsigned int TIME_STRING_PARSE_COMPONENT_OUT_OF_RANGE = 10103;
        constexpr unsigned int TIME_FORMAT_INVALID_MYSQL_TIME_STRUCT = 10104;
        constexpr unsigned int TIME_FORMAT_STREAM_ERROR = 10105;
        constexpr unsigned int TIME_CHRONO_CONVERSION_INVALID_MYSQL_TIME = 10106;  // MYSQL_TIME to chrono
        constexpr unsigned int TIME_CHRONO_CONVERSION_OUT_OF_RANGE = 10107;        // chrono value out of MYSQL_TIME range or vice-versa
        constexpr unsigned int TIME_CHRONO_CONVERSION_UNSUPPORTED_TYPE = 10108;    // e.g. trying to convert TIME to full time_point

        // MYSQL_BIND 设置错误码 (10200 - 10299)
        constexpr unsigned int BIND_SETUP_NULL_POINTER_ARGUMENT = 10201;

        // MySqlNativeValue 辅助函数错误 (10300 - 10399)
        constexpr unsigned int NATIVE_VALUE_TO_STRING_ERROR = 10301;

        // 通用逻辑/状态错误 (19000 - ...)
        constexpr unsigned int LOGIC_ERROR_INVALID_STATE = 19001;
        constexpr unsigned int UNKNOWN_ERROR = 19999;
    }  // namespace InternalErrc

    struct MySqlProtocolError {
        unsigned int error_code = InternalErrc::SUCCESS;
        char sql_state[SQLSTATE_LENGTH + 1];
        std::string error_message;

        MySqlProtocolError() noexcept {
            error_code = InternalErrc::SUCCESS;
            sql_state[0] = '0';
            sql_state[1] = '0';
            sql_state[2] = '0';
            sql_state[3] = '0';
            sql_state[4] = '0';
            sql_state[SQLSTATE_LENGTH] = '\0';
            error_message = "Success";
        }

        MySqlProtocolError(unsigned int mysql_err_code, const char* mysql_sql_state, std::string mysql_msg) noexcept : error_code(mysql_err_code), error_message(std::move(mysql_msg)) {
            if (mysql_sql_state) {
                std::strncpy(sql_state, mysql_sql_state, SQLSTATE_LENGTH);
                sql_state[SQLSTATE_LENGTH] = '\0';
            } else {  // 如果 mysql_sql_state 为空，也提供一个默认值
                sql_state[0] = 'H';
                sql_state[1] = 'Y';
                sql_state[2] = '0';
                sql_state[3] = '0';
                sql_state[4] = '0';  // "HY000" General error
                sql_state[SQLSTATE_LENGTH] = '\0';
            }
            // 确保如果 MySQL 返回错误码 0，我们的 error_message 也反映成功（即使 mysql_error 可能返回非空字符串）
            if (error_code == 0 && (mysql_sql_state == nullptr || std::strncmp(mysql_sql_state, "00000", SQLSTATE_LENGTH) != 0)) {
                // 如果 mysql_errno 是 0，但 sqlstate 不是 "00000" (或 null)，这很罕见。
                // 我们的 error_message 应该优先反映成功状态。
                if (error_message.empty() || error_message == "NULL") {  // "NULL" string is sometimes returned by mysql_error
                    this->error_message = "Success (MySQL error code 0)";
                } else if (error_message.find(" অভ") != std::string::npos) {  // Common non-error "OK" messages in some locales
                    this->error_message = "Success (MySQL: " + error_message + ")";
                } else if (error_code == 0) {  // If truly error_code 0, ensure message doesn't mislead
                    this->error_message = "Success (MySQL error code 0, non-standard state: " + std::string(sql_state) + ")";
                }
            }
        }

        MySqlProtocolError(unsigned int internal_code, std::string msg) noexcept : error_code(internal_code), error_message(std::move(msg)) {
            // 对于内部错误，error_code 是 InternalErrc::* 之一
            // sql_state 可以设置为一个通用的内部错误状态
            sql_state[0] = 'P';
            sql_state[1] = 'I';
            sql_state[2] = '0';
            sql_state[3] = '0';
            sql_state[4] = '0';  // "PI000" for Protocol Internal
            sql_state[SQLSTATE_LENGTH] = '\0';
        }

        bool isOk() const noexcept {
            // 主要判断标准：协议层内部错误码是否为 SUCCESS
            return error_code == InternalErrc::SUCCESS;
        }
    };

    struct MySqlNativeValue {
        std::variant<std::monostate, bool, int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double, std::string, std::vector<unsigned char>, MYSQL_TIME> data;

        enum enum_field_types original_mysql_type = ::MYSQL_TYPE_NULL;
        unsigned int original_mysql_flags = 0;
        uint16_t original_charsetnr = 0;  // 新增：存储原始字符集编号

        MySqlNativeValue() = default;
        bool is_null() const noexcept {
            return data.index() == 0;
        }

        std::expected<std::string, MySqlProtocolError> toString() const;

        template <typename T>
        std::optional<T> get_if() const noexcept {
            if (std::holds_alternative<T>(data)) {
                return std::get<T>(data);
            }
            return std::nullopt;
        }

        template <typename T>
        std::expected<T, MySqlProtocolError> get_as() const noexcept {
            if (std::holds_alternative<T>(data)) {
                return std::get<T>(data);
            }
            // 简单的类型名称获取，可以根据需要扩展
            std::string requested_type_name = "unknown_requested_type";
            if constexpr (std::is_same_v<T, bool>)
                requested_type_name = "bool";
            else if constexpr (std::is_same_v<T, int8_t>)
                requested_type_name = "int8_t";
            else if constexpr (std::is_same_v<T, uint8_t>)
                requested_type_name = "uint8_t";
            else if constexpr (std::is_same_v<T, int16_t>)
                requested_type_name = "int16_t";
            else if constexpr (std::is_same_v<T, uint16_t>)
                requested_type_name = "uint16_t";
            else if constexpr (std::is_same_v<T, int32_t>)
                requested_type_name = "int32_t";
            else if constexpr (std::is_same_v<T, uint32_t>)
                requested_type_name = "uint32_t";
            else if constexpr (std::is_same_v<T, int64_t>)
                requested_type_name = "int64_t";
            else if constexpr (std::is_same_v<T, uint64_t>)
                requested_type_name = "uint64_t";
            else if constexpr (std::is_same_v<T, float>)
                requested_type_name = "float";
            else if constexpr (std::is_same_v<T, double>)
                requested_type_name = "double";
            else if constexpr (std::is_same_v<T, std::string>)
                requested_type_name = "std::string";
            else if constexpr (std::is_same_v<T, std::vector<unsigned char>>)
                requested_type_name = "std::vector<unsigned char>";
            else if constexpr (std::is_same_v<T, MYSQL_TIME>)
                requested_type_name = "MYSQL_TIME";

            return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_TYPE_MISMATCH_ACCESS, "Attempted to get value as type '" + requested_type_name + "' but it holds a different type. Original MySQL type ID: " + std::to_string(original_mysql_type)));
        }
    };

    // --- MySQL Native Value Conversion Functions (Declarations) ---
    std::expected<MySqlNativeValue, MySqlProtocolError> mySqlRowFieldToNativeValue(const char* c_str_value, unsigned long length, const MYSQL_FIELD* field_meta);
    std::expected<MySqlNativeValue, MySqlProtocolError> mySqlBoundResultToNativeValue(const MYSQL_BIND* bind_info, unsigned int original_flags_if_known = 0, uint16_t original_charsetnr_if_known = 0);

    // --- MYSQL_BIND Preparation for Statement Parameters (Declarations) ---
    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool);
    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool is_unsigned, int8_t);
    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool is_unsigned, int16_t);
    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool is_unsigned, int32_t);
    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool is_unsigned, int64_t);
    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, float);
    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, double);

    std::expected<void, MySqlProtocolError> setupMySqlBindForInputString(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, unsigned long* length_indicator_ptr, enum enum_field_types mysql_type, char* str_buffer, unsigned long str_actual_length);
    std::expected<void, MySqlProtocolError> setupMySqlBindForInputBlob(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, unsigned long* length_indicator_ptr, enum enum_field_types mysql_type, unsigned char* blob_buffer, unsigned long blob_actual_length);
    std::expected<void, MySqlProtocolError> setupMySqlBindForInputTime(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, enum enum_field_types mysql_type, MYSQL_TIME* time_buffer);
    std::expected<void, MySqlProtocolError> setupMySqlBindForNull(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, enum enum_field_types mysql_type);

    // --- MYSQL_TIME <-> String Conversion Utilities (Declarations) ---
    std::expected<MYSQL_TIME, MySqlProtocolError> parseDateTimeStringToMySqlTime(std::string_view dt_string, enum enum_field_types expected_type);
    std::expected<std::string, MySqlProtocolError> formatMySqlTimeToString(const MYSQL_TIME& mysql_time, enum enum_field_types original_type);

    // --- MYSQL_TIME <-> std::chrono Conversion Utilities (Declarations) ---
    std::expected<std::chrono::system_clock::time_point, MySqlProtocolError> mySqlTimeToSystemClockTimePoint(const MYSQL_TIME& mysql_time);
    std::expected<MYSQL_TIME, MySqlProtocolError> systemClockTimePointToMySqlTime(const std::chrono::system_clock::time_point& time_point, enum enum_field_types target_mysql_type = MYSQL_TYPE_DATETIME);

    // C++20 date/time types might be more direct for some conversions
    // For DATE:
    std::expected<std::chrono::year_month_day, MySqlProtocolError> mySqlTimeToYearMonthDay(const MYSQL_TIME& mysql_time);
    std::expected<MYSQL_TIME, MySqlProtocolError> yearMonthDayToMySqlDate(const std::chrono::year_month_day& ymd);

    // For TIME: (duration from midnight)
    std::expected<std::chrono::microseconds, MySqlProtocolError> mySqlTimeToDuration(const MYSQL_TIME& mysql_time);  // Returns duration, handles 'neg'
    std::expected<MYSQL_TIME, MySqlProtocolError> durationToMySqlTime(std::chrono::microseconds duration_from_midnight);

    // --- MySQL Error Reporting (Declarations) ---
    MySqlProtocolError getMySqlHandleError(MYSQL* handle);
    MySqlProtocolError getMySqlStmtError(MYSQL_STMT* stmt_handle);

}  // namespace mysql_protocol