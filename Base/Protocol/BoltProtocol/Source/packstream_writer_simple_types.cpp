#include <cstring>    // For memcpy (for float64)
#include <exception>  // For std::bad_alloc (though less direct here)
#include <limits>     // For std::numeric_limits

#include "boltprotocol/message_defs.h"       // For BoltError (though packstream_writer.h includes it)
#include "boltprotocol/packstream_writer.h"  // For PackStreamWriter class declaration and constants
// byte_order_utils.h is included via packstream_writer.h -> detail/byte_order_utils.h

namespace boltprotocol {

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
        BoltError err = BoltError::SUCCESS;  // Initialize err

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
        } else {  // INT_64
            err = append_byte(MARKER_INT_64);
            if (err == BoltError::SUCCESS) err = append_network_int(int_value);
        }
        return err;  // Return the result of the last append operation
    }

    BoltError PackStreamWriter::write_float_internal(double float_value) {
        if (has_error()) return error_state_;
        BoltError err = append_byte(MARKER_FLOAT64);
        if (err != BoltError::SUCCESS) return err;  // If appending marker failed

        uint64_t temp_int;  // To hold byte representation of double
        static_assert(sizeof(double) == sizeof(uint64_t), "Double is not 64-bit.");
        std::memcpy(&temp_int, &float_value, sizeof(double));

        // append_network_int will handle host-to-be conversion for temp_int
        return append_network_int(temp_int);
    }

}  // namespace boltprotocol