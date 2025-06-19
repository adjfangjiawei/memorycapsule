// Source/mysql_protocol/mysql_native_value_utils.cpp
#include <charconv>  // For std::to_chars (C++17, usable in C++26 context) for floats/doubles if needed
#include <iomanip>   // For std::hex, std::setw, std::setfill
#include <sstream>   // For std::ostringstream
#include <string>    // For std::to_string, std::string
#include <variant>   // For std::visit
#include <vector>    // For std::vector

#include "mysql_protocol/mysql_type_converter.h"  // For MySqlNativeValue, MySqlProtocolError, formatMySqlTimeToString

// <mysql/mysql.h>, <expected> are included via mysql_type_converter.h

namespace mysql_protocol {

    // Helper to convert std::vector<unsigned char> to hex string
    std::string blobToHexString(const std::vector<unsigned char>& blob) {
        std::ostringstream oss;
        oss << "0x";
        oss << std::hex << std::setfill('0');
        for (unsigned char byte : blob) {
            oss << std::setw(2) << static_cast<int>(byte);
        }
        return oss.str();
    }

    std::expected<std::string, MySqlProtocolError> MySqlNativeValue::toString() const {
        return std::visit(
            [this](auto&& arg) -> std::expected<std::string, MySqlProtocolError> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return "NULL";
                } else if constexpr (std::is_same_v<T, bool>) {
                    return arg ? "true" : "false";
                } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
                    // For floats/doubles, std::to_string can lose precision or have locale issues.
                    // std::format (C++20) or std::to_chars (C++17) are better alternatives for production.
                    // Using std::to_string for simplicity here, assuming default locale is fine.
                    // For C++26, std::format would be ideal.
                    if constexpr (std::is_floating_point_v<T>) {
                        // Using ostringstream for better control over float/double formatting
                        std::ostringstream temp_oss;
                        temp_oss << arg;  // Default precision
                        if (temp_oss.fail()) {
                            return std::unexpected(MySqlProtocolError(InternalErrc::NATIVE_VALUE_TO_STRING_ERROR, "Failed to convert float/double to string using ostringstream."));
                        }
                        return temp_oss.str();
                    } else {
                        return std::to_string(arg);
                    }
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return "'" + arg + "'";  // Quote strings
                } else if constexpr (std::is_same_v<T, std::vector<unsigned char>>) {
                    return blobToHexString(arg);
                } else if constexpr (std::is_same_v<T, MYSQL_TIME>) {
                    // Use the existing formatting function. original_mysql_type should be set correctly
                    // when MySqlNativeValue was created.
                    return formatMySqlTimeToString(arg, this->original_mysql_type);
                }
                // Fallback for any unhandled type in the variant (should not happen if variant is exhaustive)
                return std::unexpected(MySqlProtocolError(InternalErrc::NATIVE_VALUE_TO_STRING_ERROR, "Unhandled type in MySqlNativeValue::toString."));
            },
            data);
    }

}  // namespace mysql_protocol