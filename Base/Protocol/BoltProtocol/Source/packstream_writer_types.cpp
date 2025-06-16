#include <cstring>    // For memcpy (for float)
#include <exception>  // For std::bad_alloc, std::exception (though not directly caught here, called functions might)
#include <limits>     // For std::numeric_limits

#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    // --- PackStreamWriter Type-Specific Internal Implementations ---

    BoltError PackStreamWriter::write_null_internal() {
        if (has_error()) return error_state_;
        return append_byte(MARKER_NULL);
    }

    BoltError PackStreamWriter::write_boolean_internal(bool bool_value) {
        if (has_error()) return error_state_;
        return append_byte(bool_value ? MARKER_TRUE : MARKER_FALSE);
    }

    BoltError PackStreamWriter::write_integer_internal(int64_t int_value) {
        if (has_error()) return error_state_;
        BoltError err;
        if (int_value >= -16 && int_value <= 127) {  // Tiny Int
            err = append_byte(static_cast<uint8_t>(int_value));
        } else if (int_value >= std::numeric_limits<int8_t>::min() && int_value <= std::numeric_limits<int8_t>::max()) {
            err = append_byte(MARKER_INT_8);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<int8_t>(int_value));
        } else if (int_value >= std::numeric_limits<int16_t>::min() && int_value <= std::numeric_limits<int16_t>::max()) {
            err = append_byte(MARKER_INT_16);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<int16_t>(int_value));
        } else if (int_value >= std::numeric_limits<int32_t>::min() && int_value <= std::numeric_limits<int32_t>::max()) {
            err = append_byte(MARKER_INT_32);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<int32_t>(int_value));
        } else {
            err = append_byte(MARKER_INT_64);
            if (err == BoltError::SUCCESS) err = append_network_int(int_value);
        }
        return err;
    }

    BoltError PackStreamWriter::write_float_internal(double float_value) {
        if (has_error()) return error_state_;
        BoltError err = append_byte(MARKER_FLOAT64);
        if (err != BoltError::SUCCESS) return err;

        uint64_t temp_int;
        static_assert(sizeof(double) == sizeof(uint64_t), "Double is not 64-bit.");
        std::memcpy(&temp_int, &float_value, sizeof(double));
        return append_network_int(temp_int);
    }

    BoltError PackStreamWriter::write_string_header_internal(uint32_t size) {
        if (has_error()) return error_state_;
        BoltError err;
        if (size <= 0x0F) {  // Tiny String
            err = append_byte(MARKER_TINY_STRING_BASE | static_cast<uint8_t>(size));
        } else if (size <= std::numeric_limits<uint8_t>::max()) {
            err = append_byte(MARKER_STRING_8);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint8_t>(size));
        } else if (size <= std::numeric_limits<uint16_t>::max()) {
            err = append_byte(MARKER_STRING_16);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint16_t>(size));
        } else {  // STRING_32
            err = append_byte(MARKER_STRING_32);
            if (err == BoltError::SUCCESS) err = append_network_int(size);
        }
        return err;
    }

    BoltError PackStreamWriter::write_string_data_internal(const std::string& value_str) {
        if (has_error()) return error_state_;
        if (value_str.empty()) return BoltError::SUCCESS;  // No data to append for empty string
        return append_bytes(value_str.data(), value_str.length());
    }

    BoltError PackStreamWriter::serialize_string_internal(const std::string& str_value) {
        if (has_error()) return error_state_;
        // PackStream strings are limited to 2^32 - 1 bytes.
        // std::string::length() returns size_t, which could be larger on 64-bit.
        if (str_value.length() > std::numeric_limits<uint32_t>::max()) {
            set_error(BoltError::SERIALIZATION_ERROR);  // String too long for PackStream
            return error_state_;
        }
        uint32_t len = static_cast<uint32_t>(str_value.length());
        BoltError err = write_string_header_internal(len);
        if (err != BoltError::SUCCESS) return err;
        return write_string_data_internal(str_value);
    }

    BoltError PackStreamWriter::write_list_header_internal(uint32_t size) {
        if (has_error()) return error_state_;
        BoltError err;
        if (size <= 0x0F) {  // Tiny List
            err = append_byte(MARKER_TINY_LIST_BASE | static_cast<uint8_t>(size));
        } else if (size <= std::numeric_limits<uint8_t>::max()) {
            err = append_byte(MARKER_LIST_8);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint8_t>(size));
        } else if (size <= std::numeric_limits<uint16_t>::max()) {
            err = append_byte(MARKER_LIST_16);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint16_t>(size));
        } else {  // LIST_32
            err = append_byte(MARKER_LIST_32);
            if (err == BoltError::SUCCESS) err = append_network_int(size);
        }
        return err;
    }

    BoltError PackStreamWriter::serialize_list_internal(const BoltList& list_data) {
        if (has_error()) return error_state_;
        const auto& list_elements = list_data.elements;
        if (list_elements.size() > std::numeric_limits<uint32_t>::max()) {
            set_error(BoltError::SERIALIZATION_ERROR);  // List too large
            return error_state_;
        }
        if (current_recursion_depth_ >= MAX_RECURSION_DEPTH) {
            set_error(BoltError::RECURSION_DEPTH_EXCEEDED);
            return error_state_;
        }
        current_recursion_depth_++;

        BoltError err = write_list_header_internal(static_cast<uint32_t>(list_elements.size()));
        if (err != BoltError::SUCCESS) {
            current_recursion_depth_--;
            return err;
        }

        for (const auto& item : list_elements) {
            err = this->write(item);  // Recursive call to main write
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                // error_state_ is already set by the recursive call to write()
                return error_state_;
            }
        }
        current_recursion_depth_--;
        return BoltError::SUCCESS;
    }

    BoltError PackStreamWriter::write_map_header_internal(uint32_t size) {
        if (has_error()) return error_state_;
        BoltError err;
        if (size <= 0x0F) {  // Tiny Map
            err = append_byte(MARKER_TINY_MAP_BASE | static_cast<uint8_t>(size));
        } else if (size <= std::numeric_limits<uint8_t>::max()) {
            err = append_byte(MARKER_MAP_8);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint8_t>(size));
        } else if (size <= std::numeric_limits<uint16_t>::max()) {
            err = append_byte(MARKER_MAP_16);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint16_t>(size));
        } else {  // MAP_32
            err = append_byte(MARKER_MAP_32);
            if (err == BoltError::SUCCESS) err = append_network_int(size);
        }
        return err;
    }

    BoltError PackStreamWriter::serialize_map_internal(const BoltMap& map_data) {
        if (has_error()) return error_state_;
        const auto& map_pairs = map_data.pairs;
        if (map_pairs.size() > std::numeric_limits<uint32_t>::max()) {
            set_error(BoltError::SERIALIZATION_ERROR);  // Map too large
            return error_state_;
        }

        if (current_recursion_depth_ >= MAX_RECURSION_DEPTH) {
            set_error(BoltError::RECURSION_DEPTH_EXCEEDED);
            return error_state_;
        }
        current_recursion_depth_++;

        BoltError err = write_map_header_internal(static_cast<uint32_t>(map_pairs.size()));
        if (err != BoltError::SUCCESS) {
            current_recursion_depth_--;
            return err;
        }

        for (const auto& pair : map_pairs) {
            err = serialize_string_internal(pair.first);  // Write key (string)
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;  // Error already set
            }
            err = this->write(pair.second);  // Write value (recursive)
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;  // Error already set
            }
        }
        current_recursion_depth_--;
        return BoltError::SUCCESS;
    }

    BoltError PackStreamWriter::write_struct_header_internal(uint8_t tag, uint32_t size) {
        if (has_error()) return error_state_;
        BoltError err;
        if (size <= 0x0F) {  // Tiny Struct
            err = append_byte(MARKER_TINY_STRUCT_BASE | static_cast<uint8_t>(size));
            if (err == BoltError::SUCCESS) err = append_byte(tag);
        } else if (size <= std::numeric_limits<uint8_t>::max()) {  // Struct 8
            err = append_byte(MARKER_STRUCT_8);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint8_t>(size));
            if (err == BoltError::SUCCESS) err = append_byte(tag);
        } else if (size <= std::numeric_limits<uint16_t>::max()) {  // Struct 16
            err = append_byte(MARKER_STRUCT_16);
            if (err == BoltError::SUCCESS) err = append_network_int(static_cast<uint16_t>(size));
            if (err == BoltError::SUCCESS) err = append_byte(tag);
        } else {
            // PackStream v1 (Bolt) does not define STRUCT_32. Max fields for a struct is 65535.
            set_error(BoltError::SERIALIZATION_ERROR);  // Structure too large
            return error_state_;
        }
        return err;
    }

    BoltError PackStreamWriter::serialize_structure_internal(const PackStreamStructure& struct_data) {
        if (has_error()) return error_state_;
        // Max fields for a PackStream structure that Bolt messages use is typically limited by STRUCT_16
        if (struct_data.fields.size() > 0xFFFF) {       // 65535
            set_error(BoltError::SERIALIZATION_ERROR);  // Structure too large
            return error_state_;
        }

        if (current_recursion_depth_ >= MAX_RECURSION_DEPTH) {
            set_error(BoltError::RECURSION_DEPTH_EXCEEDED);
            return error_state_;
        }
        current_recursion_depth_++;

        BoltError err = write_struct_header_internal(struct_data.tag, static_cast<uint32_t>(struct_data.fields.size()));
        if (err != BoltError::SUCCESS) {
            current_recursion_depth_--;
            return err;
        }

        for (const auto& field : struct_data.fields) {
            err = this->write(field);  // Recursive call
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;  // Error already set
            }
        }
        current_recursion_depth_--;
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol