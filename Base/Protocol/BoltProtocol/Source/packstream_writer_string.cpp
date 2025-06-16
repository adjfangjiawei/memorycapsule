#include <exception>  // For std::bad_alloc (from vector operations if buffer_ptr_)
#include <limits>     // For std::numeric_limits
#include <string>
#include <vector>  // For buffer_ptr_ if used (though append_bytes handles it)

#include "boltprotocol/message_defs.h"
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError PackStreamWriter::write_string_header_internal(uint32_t size) {
        if (has_error()) return error_state_;
        BoltError err = BoltError::SUCCESS;

        if (size <= 0x0F) {  // Tiny String (0x80 to 0x8F)
            err = append_byte(MARKER_TINY_STRING_BASE | static_cast<uint8_t>(size));
        } else if (size <= std::numeric_limits<uint8_t>::max()) {
            err = append_byte(MARKER_STRING_8);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint8_t>(size));
        } else if (size <= std::numeric_limits<uint16_t>::max()) {
            err = append_byte(MARKER_STRING_16);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint16_t>(size));
        } else {  // STRING_32 (uint32_t max is handled by PackStream spec)
            err = append_byte(MARKER_STRING_32);
            if (err == BoltError::SUCCESS) err = append_network_int(size);  // size is already uint32_t
        }
        return err;
    }

    BoltError PackStreamWriter::write_string_data_internal(const std::string& value_str) {
        if (has_error()) return error_state_;
        if (value_str.empty()) {
            return BoltError::SUCCESS;  // Nothing to append for an empty string's data
        }
        // append_bytes handles potential errors (like OUT_OF_MEMORY if writing to vector buffer)
        return append_bytes(value_str.data(), value_str.length());
    }

    BoltError PackStreamWriter::serialize_string_internal(const std::string& str_value) {
        if (has_error()) return error_state_;

        // PackStream strings are limited to 2^32 - 1 bytes (UINT32_MAX).
        // std::string::length() returns size_t.
        if (str_value.length() > std::numeric_limits<uint32_t>::max()) {
            set_error(BoltError::SERIALIZATION_ERROR);  // String too long for PackStream
            return error_state_;
        }
        uint32_t len = static_cast<uint32_t>(str_value.length());

        BoltError err = write_string_header_internal(len);
        if (err != BoltError::SUCCESS) {
            // error_state_ already set by write_string_header_internal or its callees
            return error_state_;
        }
        // Only write data if length > 0 (handled by write_string_data_internal)
        return write_string_data_internal(str_value);
    }

}  // namespace boltprotocol