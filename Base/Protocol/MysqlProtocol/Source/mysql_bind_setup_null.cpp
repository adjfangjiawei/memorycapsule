// Source/mysql_protocol/mysql_bind_setup_null.cpp
#include <cstring>  // For std::memset

#include "mysql_protocol/mysql_type_converter.h"

// <mysql/mysql.h>, <expected> are included via mysql_type_converter.h

namespace mysql_protocol {

    std::expected<void, MySqlProtocolError> setupMySqlBindForNull(MYSQL_BIND& bind_struct, bool* is_null_indicator_ptr, enum enum_field_types mysql_type) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (null setup)."));
        }

        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = mysql_type;  // Type still needs to be known by the server
        bind_struct.buffer = nullptr;          // No data buffer for NULL
        bind_struct.buffer_length = 0;         // No length for NULL

        *is_null_indicator_ptr = true;  // Mark as SQL NULL
        bind_struct.is_null = is_null_indicator_ptr;

        bind_struct.length = nullptr;  // Length is not relevant for NULL
        // is_unsigned is also not relevant for NULL
        return {};  // Success
    }

}  // namespace mysql_protocol