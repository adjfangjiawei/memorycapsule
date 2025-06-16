#include <exception>  // For std::bad_alloc, std::length_error
#include <string>
#include <vector>  // Though not directly used, good for context

#include "boltprotocol/message_defs.h"
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    // Helper to read string data of a given size into an std::string
    // This was originally in packstream_reader_types.cpp
    BoltError PackStreamReader::read_string_data_into(std::string& out_string, uint32_t size) {
        if (has_error()) return error_state_;
        out_string.clear();

        if (size == 0) {
            return BoltError::SUCCESS;
        }
        try {
            out_string.resize(size);  // Potential std::bad_alloc or std::length_error
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::length_error&) {              // String too long for std::string to handle
            set_error(BoltError::DESERIALIZATION_ERROR);  // Or MESSAGE_TOO_LARGE
            return error_state_;
        } catch (const std::exception&) {
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }

        // consume_bytes will set error_state_ on failure (e.g., not enough bytes)
        return consume_bytes(out_string.data(), size);
    }

    BoltError PackStreamReader::read_string_value(uint8_t marker, Value& out_value) {
        if (has_error()) return error_state_;
        uint32_t size = 0;
        BoltError err;

        if ((marker & 0xF0) == MARKER_TINY_STRING_BASE) {  // Tiny String (0x80 to 0x8F)
            size = marker & 0x0F;
        } else {
            switch (marker) {
                case MARKER_STRING_8:
                    {
                        uint8_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;
                        break;
                    }
                case MARKER_STRING_16:
                    {
                        uint16_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;
                        break;
                    }
                case MARKER_STRING_32:
                    {
                        uint32_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;  // Already uint32_t
                        break;
                    }
                default:
                    set_error(BoltError::INVALID_ARGUMENT);  // Marker not a string marker
                    return error_state_;
            }
        }

        std::string s_val;
        err = read_string_data_into(s_val, size);
        if (err != BoltError::SUCCESS) {
            // error_state_ is already set by read_string_data_into or consume_bytes
            return error_state_;
        }

        try {
            out_value = std::move(s_val);  // std::string move constructor into variant
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::exception&) {  // Other potential exceptions from variant assignment
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol