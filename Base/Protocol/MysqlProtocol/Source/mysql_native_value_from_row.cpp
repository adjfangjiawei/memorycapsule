#include <charconv>     // For std::from_chars
#include <limits>       // For std::numeric_limits
#include <string>       // For std::string, std::to_string
#include <string_view>  // For std::string_view
#include <vector>       // For std::vector

#include "mysql_protocol/mysql_type_converter.h"

// <mysql/mysql.h>, <expected>, <variant> are included via mysql_type_converter.h

namespace mysql_protocol {

    std::expected<MySqlNativeValue, MySqlProtocolError> mySqlRowFieldToNativeValue(const char* c_str_value, unsigned long length, const MYSQL_FIELD* field_meta) {
        if (!field_meta) {
            return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_INPUT_ARGUMENT, "MYSQL_FIELD metadata is null."));
        }

        MySqlNativeValue native_val;
        native_val.original_mysql_type = field_meta->type;
        native_val.original_mysql_flags = field_meta->flags;
        native_val.original_charsetnr = field_meta->charsetnr;

        if (c_str_value == nullptr) {
            native_val.data = std::monostate{};
            return native_val;
        }

        std::string_view sv(c_str_value, length);

        switch (field_meta->type) {
            case MYSQL_TYPE_TINY:
                if (field_meta->length == 1 && !(field_meta->flags & UNSIGNED_FLAG) && (field_meta->flags & NUM_FLAG)) {
                    if (sv == "1")
                        native_val.data = true;
                    else if (sv == "0")
                        native_val.data = false;
                    else {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "TINYINT(1) for bool expected '0' or '1', got: " + std::string(sv)));
                    }
                } else if (field_meta->flags & UNSIGNED_FLAG) {
                    unsigned long long ull_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), ull_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "TINY UNSIGNED conversion failed: invalid format for '" + std::string(sv) + "'."));
                    }
                    if (ull_val > std::numeric_limits<uint8_t>::max()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_VALUE_OUT_OF_RANGE, "TINY UNSIGNED conversion failed: value '" + std::string(sv) + "' out of range."));
                    }
                    native_val.data = static_cast<uint8_t>(ull_val);
                } else {
                    long long ll_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), ll_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "TINY SIGNED conversion failed: invalid format for '" + std::string(sv) + "'."));
                    }
                    if (ll_val < std::numeric_limits<int8_t>::min() || ll_val > std::numeric_limits<int8_t>::max()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_VALUE_OUT_OF_RANGE, "TINY SIGNED conversion failed: value '" + std::string(sv) + "' out of range."));
                    }
                    native_val.data = static_cast<int8_t>(ll_val);
                }
                break;
            case MYSQL_TYPE_SHORT:
                if (field_meta->flags & UNSIGNED_FLAG) {
                    unsigned long long ull_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), ull_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "SHORT UNSIGNED: invalid format for '" + std::string(sv) + "'."));
                    }
                    if (ull_val > std::numeric_limits<uint16_t>::max()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_VALUE_OUT_OF_RANGE, "SHORT UNSIGNED: value '" + std::string(sv) + "' out of range."));
                    }
                    native_val.data = static_cast<uint16_t>(ull_val);
                } else {
                    long long ll_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), ll_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "SHORT SIGNED: invalid format for '" + std::string(sv) + "'."));
                    }
                    if (ll_val < std::numeric_limits<int16_t>::min() || ll_val > std::numeric_limits<int16_t>::max()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_VALUE_OUT_OF_RANGE, "SHORT SIGNED: value '" + std::string(sv) + "' out of range."));
                    }
                    native_val.data = static_cast<int16_t>(ll_val);
                }
                break;
            case MYSQL_TYPE_INT24:
            case MYSQL_TYPE_LONG:
                if (field_meta->flags & UNSIGNED_FLAG) {
                    unsigned long long ull_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), ull_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "LONG/INT24 UNSIGNED: invalid format for '" + std::string(sv) + "'."));
                    }
                    if (ull_val > std::numeric_limits<uint32_t>::max()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_VALUE_OUT_OF_RANGE, "LONG/INT24 UNSIGNED: value '" + std::string(sv) + "' out of range."));
                    }
                    native_val.data = static_cast<uint32_t>(ull_val);
                } else {
                    long long ll_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), ll_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "LONG/INT24 SIGNED: invalid format for '" + std::string(sv) + "'."));
                    }
                    if (ll_val < std::numeric_limits<int32_t>::min() || ll_val > std::numeric_limits<int32_t>::max()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_VALUE_OUT_OF_RANGE, "LONG/INT24 SIGNED: value '" + std::string(sv) + "' out of range."));
                    }
                    native_val.data = static_cast<int32_t>(ll_val);
                }
                break;
            case MYSQL_TYPE_LONGLONG:
                if (field_meta->flags & UNSIGNED_FLAG) {
                    unsigned long long ull_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), ull_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "LONGLONG UNSIGNED: invalid format for '" + std::string(sv) + "'."));
                    }
                    native_val.data = ull_val;
                } else {
                    long long ll_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), ll_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "LONGLONG SIGNED: invalid format for '" + std::string(sv) + "'."));
                    }
                    native_val.data = ll_val;
                }
                break;
            case MYSQL_TYPE_FLOAT:
                {
                    float f_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), f_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "FLOAT conversion failed for '" + std::string(sv) + "'."));
                    }
                    native_val.data = f_val;
                    break;
                }
            case MYSQL_TYPE_DOUBLE:
                {
                    double d_val;
                    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), d_val);
                    if (ec != std::errc() || ptr != sv.data() + sv.size()) {
                        return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_FORMAT, "DOUBLE conversion failed for '" + std::string(sv) + "'."));
                    }
                    native_val.data = d_val;
                    break;
                }
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_NEWDECIMAL:
            case MYSQL_TYPE_ENUM:
            case MYSQL_TYPE_SET:
            case MYSQL_TYPE_YEAR:
            case MYSQL_TYPE_JSON:
                native_val.data = std::string(sv);
                break;
            case MYSQL_TYPE_DATE:
            case MYSQL_TYPE_TIME:
            case MYSQL_TYPE_DATETIME:
            case MYSQL_TYPE_TIMESTAMP:
            case MYSQL_TYPE_TIMESTAMP2:
            case MYSQL_TYPE_DATETIME2:
            case MYSQL_TYPE_TIME2:
            case MYSQL_TYPE_NEWDATE:
                {
                    auto time_result = parseDateTimeStringToMySqlTime(sv, field_meta->type);
                    if (time_result) {
                        native_val.data = time_result.value();
                    } else {
                        return std::unexpected(time_result.error());
                    }
                    break;
                }
            case MYSQL_TYPE_STRING:
            case MYSQL_TYPE_VAR_STRING:
            case MYSQL_TYPE_VARCHAR:
                // *** FIX START ***
                // 强制将这些类型（尤其是来自SHOW命令的元数据）视为字符串，
                // 除非它们同时具有BINARY_FLAG和二进制字符集。
                // 这次修复的核心是，即使没有BINARY_FLAG，之前的逻辑也可能因为其他原因（比如没有charsetnr）而误判。
                // 现在，我们默认它是字符串，除非有非常强的反向证据。
                if ((field_meta->flags & BINARY_FLAG) && field_meta->charsetnr == 63) {  // 63 is for `binary` charset
                    native_val.data = std::vector<unsigned char>(reinterpret_cast<const unsigned char*>(c_str_value), reinterpret_cast<const unsigned char*>(c_str_value) + length);
                } else {
                    native_val.data = std::string(sv);
                }
                // *** FIX END ***
                break;
            case MYSQL_TYPE_TINY_BLOB:
            case MYSQL_TYPE_MEDIUM_BLOB:
            case MYSQL_TYPE_LONG_BLOB:
            case MYSQL_TYPE_BLOB:
            case MYSQL_TYPE_GEOMETRY:
            case MYSQL_TYPE_BIT:
                native_val.data = std::vector<unsigned char>(reinterpret_cast<const unsigned char*>(c_str_value), reinterpret_cast<const unsigned char*>(c_str_value) + length);
                break;
            case MYSQL_TYPE_NULL:
                native_val.data = std::monostate{};
                break;
            default:
                // *** FIX *** 对于未知类型，也默认当作字符串处理，这对于 SHOW 命令的输出更安全。
                native_val.data = std::string(sv);
                break;
        }
        return native_val;
    }

}  // namespace mysql_protocol