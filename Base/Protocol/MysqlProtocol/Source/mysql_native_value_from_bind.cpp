// Source/mysql_protocol/mysql_native_value_from_bind.cpp
#include <string>  // For std::string, std::to_string
#include <vector>  // For std::vector

#include "mysql_protocol/mysql_type_converter.h"

// <mysql/mysql.h>, <expected>, <variant> are included via mysql_type_converter.h

namespace mysql_protocol {

    std::expected<MySqlNativeValue, MySqlProtocolError> mySqlBoundResultToNativeValue(const MYSQL_BIND* bind_info, unsigned int original_flags_if_known, uint16_t original_charsetnr_if_known) {
        if (!bind_info) {
            return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_INPUT_ARGUMENT, "MYSQL_BIND info is null."));
        }

        MySqlNativeValue native_val;
        native_val.original_mysql_type = bind_info->buffer_type;
        native_val.original_mysql_flags = original_flags_if_known;
        native_val.original_charsetnr = original_charsetnr_if_known;  // Store charset

        if (bind_info->is_null == nullptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_INVALID_INPUT_ARGUMENT, "MYSQL_BIND.is_null pointer is null."));
        }
        if (*bind_info->is_null) {
            native_val.data = std::monostate{};
            return native_val;
        }

        unsigned long length = 0;
        if (bind_info->length) {
            length = *bind_info->length;
        }

        if (!bind_info->buffer) {
            bool type_needs_buffer_even_if_empty = false;
            switch (bind_info->buffer_type) {
                case MYSQL_TYPE_TINY:
                case MYSQL_TYPE_SHORT:
                case MYSQL_TYPE_INT24:
                case MYSQL_TYPE_LONG:
                case MYSQL_TYPE_LONGLONG:
                case MYSQL_TYPE_FLOAT:
                case MYSQL_TYPE_DOUBLE:
                case MYSQL_TYPE_DATE:
                case MYSQL_TYPE_TIME:
                case MYSQL_TYPE_DATETIME:
                case MYSQL_TYPE_TIMESTAMP:
                case MYSQL_TYPE_YEAR:
                case MYSQL_TYPE_TIMESTAMP2:
                case MYSQL_TYPE_DATETIME2:
                case MYSQL_TYPE_TIME2:
                case MYSQL_TYPE_NEWDATE:
                    type_needs_buffer_even_if_empty = true;
                    break;
                // For string/blob types, if length is 0, buffer *could* be null, though C API usually provides a valid pointer.
                // If length > 0, buffer must not be null.
                case MYSQL_TYPE_STRING:
                case MYSQL_TYPE_VAR_STRING:
                case MYSQL_TYPE_VARCHAR:
                case MYSQL_TYPE_DECIMAL:
                case MYSQL_TYPE_NEWDECIMAL:
                case MYSQL_TYPE_ENUM:
                case MYSQL_TYPE_SET:
                case MYSQL_TYPE_JSON:
                case MYSQL_TYPE_TINY_BLOB:
                case MYSQL_TYPE_MEDIUM_BLOB:
                case MYSQL_TYPE_LONG_BLOB:
                case MYSQL_TYPE_BLOB:
                case MYSQL_TYPE_GEOMETRY:
                case MYSQL_TYPE_BIT:
                    if (length > 0) type_needs_buffer_even_if_empty = true;
                    break;
                default:  // Unknown types, assume they need a buffer if not null
                    type_needs_buffer_even_if_empty = true;
                    break;
            }
            if (type_needs_buffer_even_if_empty) {
                return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "MYSQL_BIND buffer is null unexpectedly for type: " + std::to_string(bind_info->buffer_type)));
            }
            // If we reach here, buffer is null, but it's for a 0-length string/blob, or a type that doesn't strictly need a buffer if null (unlikely for bind output)
            // The switch cases for string/blob will handle creating empty string/vector.
        }

        switch (bind_info->buffer_type) {
            case MYSQL_TYPE_TINY:
                if (!bind_info->buffer && bind_info->buffer_length > 0) {
                    return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for TINY with non-zero buffer_length."));
                }
                if (bind_info->buffer_length == 1 && !bind_info->is_unsigned && (original_flags_if_known & NUM_FLAG)) {
                    if (!bind_info->buffer) return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for TINY bool heuristic."));
                    native_val.data = (*static_cast<char*>(bind_info->buffer) != 0);
                } else if (bind_info->is_unsigned) {
                    if (!bind_info->buffer) return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for unsigned TINY."));
                    native_val.data = static_cast<uint8_t>(*static_cast<unsigned char*>(bind_info->buffer));
                } else {
                    if (!bind_info->buffer) return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for signed TINY."));
                    native_val.data = static_cast<int8_t>(*static_cast<char*>(bind_info->buffer));
                }
                break;
            case MYSQL_TYPE_SHORT:
                if (!bind_info->buffer) return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for SHORT."));
                native_val.data = bind_info->is_unsigned ? static_cast<uint16_t>(*static_cast<unsigned short*>(bind_info->buffer)) : static_cast<int16_t>(*static_cast<short*>(bind_info->buffer));
                break;
            case MYSQL_TYPE_INT24:
            case MYSQL_TYPE_LONG:
                if (!bind_info->buffer) return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for LONG/INT24."));
                native_val.data = bind_info->is_unsigned ? static_cast<uint32_t>(*static_cast<unsigned int*>(bind_info->buffer)) : static_cast<int32_t>(*static_cast<int*>(bind_info->buffer));
                break;
            case MYSQL_TYPE_LONGLONG:
                if (!bind_info->buffer) return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for LONGLONG."));
                native_val.data = bind_info->is_unsigned ? static_cast<uint64_t>(*static_cast<unsigned long long*>(bind_info->buffer)) : static_cast<int64_t>(*static_cast<long long*>(bind_info->buffer));
                break;
            case MYSQL_TYPE_FLOAT:
                if (!bind_info->buffer) return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for FLOAT."));
                native_val.data = *static_cast<float*>(bind_info->buffer);
                break;
            case MYSQL_TYPE_DOUBLE:
                if (!bind_info->buffer) return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for DOUBLE."));
                native_val.data = *static_cast<double*>(bind_info->buffer);
                break;
            case MYSQL_TYPE_STRING:
            case MYSQL_TYPE_VAR_STRING:
            case MYSQL_TYPE_VARCHAR:
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_NEWDECIMAL:
            case MYSQL_TYPE_ENUM:
            case MYSQL_TYPE_SET:
            case MYSQL_TYPE_JSON:
                if (length > 0 && !bind_info->buffer) {
                    return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for non-empty string type (type: " + std::to_string(bind_info->buffer_type) + ")."));
                }
                native_val.data = bind_info->buffer ? std::string(static_cast<char*>(bind_info->buffer), length) : std::string();
                break;
            case MYSQL_TYPE_TINY_BLOB:
            case MYSQL_TYPE_MEDIUM_BLOB:
            case MYSQL_TYPE_LONG_BLOB:
            case MYSQL_TYPE_BLOB:
            case MYSQL_TYPE_GEOMETRY:
            case MYSQL_TYPE_BIT:
                if (length > 0 && !bind_info->buffer) {
                    return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for non-empty blob type (type: " + std::to_string(bind_info->buffer_type) + ")."));
                }
                native_val.data = bind_info->buffer ? std::vector<unsigned char>(static_cast<unsigned char*>(bind_info->buffer), static_cast<unsigned char*>(bind_info->buffer) + length) : std::vector<unsigned char>();
                break;
            case MYSQL_TYPE_DATE:
            case MYSQL_TYPE_TIME:
            case MYSQL_TYPE_DATETIME:
            case MYSQL_TYPE_TIMESTAMP:
            case MYSQL_TYPE_YEAR:
            case MYSQL_TYPE_TIMESTAMP2:
            case MYSQL_TYPE_DATETIME2:
            case MYSQL_TYPE_TIME2:
            case MYSQL_TYPE_NEWDATE:
                if (!bind_info->buffer) return std::unexpected(MySqlProtocolError(InternalErrc::LOGIC_ERROR_INVALID_STATE, "Null buffer for TIME type (type: " + std::to_string(bind_info->buffer_type) + ")."));
                native_val.data = *static_cast<MYSQL_TIME*>(bind_info->buffer);
                break;
            case MYSQL_TYPE_NULL:
                native_val.data = std::monostate{};
                break;
            default:
                return std::unexpected(MySqlProtocolError(InternalErrc::CONVERSION_UNSUPPORTED_TYPE, "Unsupported MySQL field type encountered in binary protocol: " + std::to_string(bind_info->buffer_type)));
        }
        return native_val;
    }

}  // namespace mysql_protocol