// Source/mysql_protocol/mysql_bind_setup_numeric.cpp
#include <cstring>  // For std::memset

#include "mysql_protocol/mysql_type_converter.h"

// <mysql/mysql.h>, <expected> are included via mysql_type_converter.h

namespace mysql_protocol {

    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool /*value_to_bind_type_deduction_only*/) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (bool)."));
        }
        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = MYSQL_TYPE_TINY;  // Booleans are often bound as TINYINT
        bind_struct.buffer_length = sizeof(char);   // MySQL C API often uses char for bool (0 or 1)
        bind_struct.is_unsigned = 0;                // Typically signed tinyint(1)
        *is_null_indicator_ptr = false;             // Assume not null by default, caller sets buffer and can update this
        bind_struct.is_null = is_null_indicator_ptr;
        // bind_struct.buffer is set by the caller (Transport layer)
        return {};  // Success
    }

    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool is_unsigned_val, int8_t) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (int8_t)."));
        }
        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = MYSQL_TYPE_TINY;
        bind_struct.buffer_length = sizeof(int8_t);
        bind_struct.is_unsigned = is_unsigned_val ? 1 : 0;
        *is_null_indicator_ptr = false;
        bind_struct.is_null = is_null_indicator_ptr;
        return {};
    }

    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool is_unsigned_val, int16_t) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (int16_t)."));
        }
        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = MYSQL_TYPE_SHORT;
        bind_struct.buffer_length = sizeof(int16_t);
        bind_struct.is_unsigned = is_unsigned_val ? 1 : 0;
        *is_null_indicator_ptr = false;
        bind_struct.is_null = is_null_indicator_ptr;
        return {};
    }

    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool is_unsigned_val, int32_t) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (int32_t)."));
        }
        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = MYSQL_TYPE_LONG;
        bind_struct.buffer_length = sizeof(int32_t);
        bind_struct.is_unsigned = is_unsigned_val ? 1 : 0;
        *is_null_indicator_ptr = false;
        bind_struct.is_null = is_null_indicator_ptr;
        return {};
    }

    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, bool is_unsigned_val, int64_t) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (int64_t)."));
        }
        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = MYSQL_TYPE_LONGLONG;
        bind_struct.buffer_length = sizeof(int64_t);
        bind_struct.is_unsigned = is_unsigned_val ? 1 : 0;
        *is_null_indicator_ptr = false;
        bind_struct.is_null = is_null_indicator_ptr;
        return {};
    }

    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, float) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (float)."));
        }
        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = MYSQL_TYPE_FLOAT;
        bind_struct.buffer_length = sizeof(float);
        bind_struct.is_unsigned = 0;  // Floats are not unsigned in MySQL bind context
        *is_null_indicator_ptr = false;
        bind_struct.is_null = is_null_indicator_ptr;
        return {};
    }

    std::expected<void, MySqlProtocolError> setupMySqlBindForInput(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, double) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (double)."));
        }
        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = MYSQL_TYPE_DOUBLE;
        bind_struct.buffer_length = sizeof(double);
        bind_struct.is_unsigned = 0;
        *is_null_indicator_ptr = false;
        bind_struct.is_null = is_null_indicator_ptr;
        return {};
    }

}  // namespace mysql_protocol