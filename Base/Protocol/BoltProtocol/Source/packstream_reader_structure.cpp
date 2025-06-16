#include <exception>  // For std::bad_alloc, std::length_error
#include <memory>     // For std::shared_ptr, std::make_shared
#include <vector>

#include "boltprotocol/message_defs.h"  // For PackStreamStructure, Value
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    // Helper to read Structure fields into a pre-allocated PackStreamStructure shared_ptr
    BoltError PackStreamReader::read_struct_fields_into(std::shared_ptr<PackStreamStructure>& struct_sptr, uint8_t tag, uint32_t size) {
        if (has_error()) return error_state_;
        if (!struct_sptr) {
            set_error(BoltError::INVALID_ARGUMENT);
            return error_state_;
        }

        struct_sptr->tag = tag;  // Set the tag

        if (current_recursion_depth_ >= MAX_RECURSION_DEPTH) {
            set_error(BoltError::RECURSION_DEPTH_EXCEEDED);
            return error_state_;
        }
        current_recursion_depth_++;

        BoltError err;
        try {
            struct_sptr->fields.reserve(size);  // Potential std::bad_alloc or std::length_error
        } catch (const std::bad_alloc&) {
            current_recursion_depth_--;
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::length_error&) {
            current_recursion_depth_--;
            set_error(BoltError::DESERIALIZATION_ERROR);  // Size too large
            return error_state_;
        } catch (const std::exception&) {
            current_recursion_depth_--;
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }

        for (uint32_t i = 0; i < size; ++i) {
            Value field_val;
            err = this->read(field_val);  // Recursive call
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;
            }
            try {
                struct_sptr->fields.push_back(std::move(field_val));
            } catch (const std::bad_alloc&) {
                current_recursion_depth_--;
                set_error(BoltError::OUT_OF_MEMORY);
                return error_state_;
            } catch (const std::exception&) {
                current_recursion_depth_--;
                set_error(BoltError::UNKNOWN_ERROR);
                return error_state_;
            }
        }
        current_recursion_depth_--;
        return BoltError::SUCCESS;
    }

    BoltError PackStreamReader::read_struct_value(uint8_t marker, Value& out_value) {
        if (has_error()) return error_state_;
        uint32_t size = 0;
        uint8_t tag = 0;
        BoltError err;

        if ((marker & 0xF0) == MARKER_TINY_STRUCT_BASE) {  // Tiny Struct (0xB0 to 0xBF)
            size = marker & 0x0F;
            err = consume_byte(tag);  // Read the tag byte following the marker
            if (err != BoltError::SUCCESS) return err;
        } else {
            switch (marker) {
                case MARKER_STRUCT_8:
                    {
                        uint8_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;

                        err = consume_byte(tag);  // Read the tag byte
                        if (err != BoltError::SUCCESS) return err;
                        break;
                    }
                case MARKER_STRUCT_16:
                    {
                        uint16_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;

                        err = consume_byte(tag);  // Read the tag byte
                        if (err != BoltError::SUCCESS) return err;
                        break;
                    }
                default:
                    set_error(BoltError::INVALID_ARGUMENT);
                    return error_state_;
            }
        }

        std::shared_ptr<PackStreamStructure> struct_sptr;
        try {
            struct_sptr = std::make_shared<PackStreamStructure>();
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::exception&) {
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }

        err = read_struct_fields_into(struct_sptr, tag, size);
        if (err != BoltError::SUCCESS) return err;

        try {
            out_value = std::move(struct_sptr);
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::exception&) {
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol