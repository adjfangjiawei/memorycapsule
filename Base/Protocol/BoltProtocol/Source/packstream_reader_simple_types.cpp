#include <cstring>    // For memcpy (for float64)
#include <exception>  // For std::bad_alloc (though less likely here, more in complex types)
#include <limits>     // For std::numeric_limits (though not strictly needed for existing integer logic)

#include "boltprotocol/message_defs.h"       // For Value, BoltError
#include "boltprotocol/packstream_reader.h"  // For PackStreamReader class declaration and constants
// byte_order_utils.h is included via packstream_reader.h -> detail/byte_order_utils.h

namespace boltprotocol {

    BoltError PackStreamReader::read_null_value(Value& out_value) {
        if (has_error()) return error_state_;
        out_value = nullptr;
        return BoltError::SUCCESS;
    }

    BoltError PackStreamReader::read_boolean_value(bool bool_val_from_marker, Value& out_value) {
        if (has_error()) return error_state_;
        out_value = bool_val_from_marker;
        return BoltError::SUCCESS;
    }

    BoltError PackStreamReader::read_float64_value(Value& out_value) {
        if (has_error()) return error_state_;
        uint64_t temp_int;
        BoltError err = consume_network_int(temp_int);  // consume_network_int handles byte swapping
        if (err != BoltError::SUCCESS) return err;

        double val;
        // Ensure that double is 64-bit and has the same endianness concerns as uint64_t
        // The value read into temp_int is already in host byte order.
        static_assert(sizeof(double) == sizeof(uint64_t), "Double is not 64-bit.");
        std::memcpy(&val, &temp_int, sizeof(double));

        try {
            out_value = val;
        } catch (const std::bad_alloc&) {  // Value variant assignment might allocate if it's a complex type, though not for double. Defensive.
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        }
        return BoltError::SUCCESS;
    }

    BoltError PackStreamReader::read_integer_value(uint8_t marker, Value& out_value) {
        if (has_error()) return error_state_;
        BoltError err;
        int64_t final_val = 0;

        switch (marker) {
            case MARKER_INT_8:
                {
                    int8_t val;
                    err = consume_network_int(val);
                    if (err != BoltError::SUCCESS) return err;
                    final_val = static_cast<int64_t>(val);
                    break;
                }
            case MARKER_INT_16:
                {
                    int16_t val;
                    err = consume_network_int(val);
                    if (err != BoltError::SUCCESS) return err;
                    final_val = static_cast<int64_t>(val);
                    break;
                }
            case MARKER_INT_32:
                {
                    int32_t val;
                    err = consume_network_int(val);
                    if (err != BoltError::SUCCESS) return err;
                    final_val = static_cast<int64_t>(val);
                    break;
                }
            case MARKER_INT_64:
                {
                    int64_t val;
                    err = consume_network_int(val);
                    if (err != BoltError::SUCCESS) return err;
                    final_val = val;
                    break;
                }
            default:
                // This case should ideally not be reached if dispatch in `read()` is correct
                set_error(BoltError::INVALID_ARGUMENT);
                return error_state_;
        }

        try {
            out_value = final_val;
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol