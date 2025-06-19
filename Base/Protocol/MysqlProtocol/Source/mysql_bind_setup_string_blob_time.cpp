// Source/mysql_protocol/mysql_bind_setup_string_blob_time.cpp
#include <cstring>  // For std::memset

#include "mysql_protocol/mysql_type_converter.h"

// <mysql/mysql.h>, <expected> are included via mysql_type_converter.h

namespace mysql_protocol {

    std::expected<void, MySqlProtocolError> setupMySqlBindForInputString(MYSQL_BIND& bind_struct,
                                                                         bool* is_null_indicator_ptr,
                                                                         unsigned long* length_indicator_ptr,
                                                                         enum enum_field_types mysql_type,
                                                                         char* str_buffer,  // Buffer provided by caller
                                                                         unsigned long str_actual_length) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (string)."));
        }
        if (!length_indicator_ptr) {
            // For strings/blobs, length_indicator_ptr is crucial.
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "length_indicator_ptr cannot be null for MYSQL_BIND (string)."));
        }
        // str_buffer can be null IF str_actual_length is 0, representing an empty string.
        // However, the C API might expect a non-null (even if dummy) buffer for empty strings.
        // Let's assume caller manages this; if str_actual_length > 0, str_buffer must be valid.
        if (str_actual_length > 0 && !str_buffer) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "str_buffer cannot be null for non-empty string."));
        }

        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = mysql_type;  // e.g., MYSQL_TYPE_STRING, MYSQL_TYPE_VAR_STRING, MYSQL_TYPE_JSON
        bind_struct.buffer = static_cast<void*>(str_buffer);
        // For input, buffer_length is the max capacity of the buffer.
        // The actual length of data is given by *bind_struct.length.
        // Let's assume str_actual_length is the actual data length, and the buffer is at least this large.
        // The C API documentation for mysql_stmt_bind_param specifies buffer_length as "the size of the buffer".
        // For input of variable-length data, `*length` points to the actual length.
        // It's safer to set buffer_length to the actual data length being sent for string/blob types if that's what's in str_buffer.
        bind_struct.buffer_length = str_actual_length;

        *length_indicator_ptr = str_actual_length;
        bind_struct.length = length_indicator_ptr;

        *is_null_indicator_ptr = false;  // Buffer is provided, so not SQL NULL initially
        bind_struct.is_null = is_null_indicator_ptr;
        // bind_struct.is_unsigned is not relevant for string types
        return {};  // Success
    }

    std::expected<void, MySqlProtocolError> setupMySqlBindForInputBlob(MYSQL_BIND& bind_struct,
                                                                       bool* is_null_indicator_ptr,
                                                                       unsigned long* length_indicator_ptr,
                                                                       enum enum_field_types mysql_type,
                                                                       unsigned char* blob_buffer,  // Buffer provided by caller
                                                                       unsigned long blob_actual_length) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (blob)."));
        }
        if (!length_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "length_indicator_ptr cannot be null for MYSQL_BIND (blob)."));
        }
        if (blob_actual_length > 0 && !blob_buffer) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "blob_buffer cannot be null for non-empty blob."));
        }

        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = mysql_type;  // e.g., MYSQL_TYPE_BLOB
        bind_struct.buffer = static_cast<void*>(blob_buffer);
        bind_struct.buffer_length = blob_actual_length;  // Similar to string, actual length of data in buffer

        *length_indicator_ptr = blob_actual_length;
        bind_struct.length = length_indicator_ptr;

        *is_null_indicator_ptr = false;
        bind_struct.is_null = is_null_indicator_ptr;
        return {};  // Success
    }

    std::expected<void, MySqlProtocolError> setupMySqlBindForInputTime(MYSQL_BIND& bind_struct,
                                                                       bool* is_null_indicator_ptr,
                                                                       enum enum_field_types mysql_type,
                                                                       MYSQL_TIME* time_buffer  // Buffer provided by caller
    ) {
        if (!is_null_indicator_ptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "is_null_indicator_ptr cannot be null for MYSQL_BIND (time)."));
        }
        if (!time_buffer) {
            return std::unexpected(MySqlProtocolError(InternalErrc::BIND_SETUP_NULL_POINTER_ARGUMENT, "time_buffer cannot be null for MYSQL_BIND (time)."));
        }

        std::memset(&bind_struct, 0, sizeof(MYSQL_BIND));
        bind_struct.buffer_type = mysql_type;  // e.g., MYSQL_TYPE_DATETIME, MYSQL_TYPE_TIMESTAMP
        bind_struct.buffer = static_cast<void*>(time_buffer);
        bind_struct.buffer_length = sizeof(MYSQL_TIME);  // Fixed size for MYSQL_TIME

        *is_null_indicator_ptr = false;
        bind_struct.is_null = is_null_indicator_ptr;
        // bind_struct.length is not typically used for MYSQL_TIME input binding for fixed-size types.
        // mysql_stmt_bind_param infers length from buffer_type for fixed-size types.
        // Setting it to nullptr is common.
        bind_struct.length = nullptr;
        return {};  // Success
    }

}  // namespace mysql_protocol