#include <cstring>    // For memcpy
#include <exception>  // For std::bad_alloc
#include <map>        // For std::map in BoltMap
#include <memory>     // For std::make_shared
#include <new>        // For std::nothrow (alternative for make_shared if needed)
#include <string>     // For std::string
#include <vector>     // For std::vector

#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    // --- PackStreamReader Type-Specific Reader Implementations (returning BoltError) ---

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
        BoltError err = consume_network_int(temp_int);
        if (err != BoltError::SUCCESS) return err;

        double val;
        static_assert(sizeof(double) == sizeof(uint64_t), "Double is not 64-bit.");
        std::memcpy(&val, &temp_int, sizeof(double));
        out_value = val;
        return BoltError::SUCCESS;
    }

    BoltError PackStreamReader::read_integer_value(uint8_t marker, Value& out_value) {
        if (has_error()) return error_state_;
        BoltError err;
        switch (marker) {
            case MARKER_INT_8:
                {
                    int8_t val;
                    err = consume_network_int(val);
                    if (err != BoltError::SUCCESS) return err;
                    out_value = static_cast<int64_t>(val);
                    return BoltError::SUCCESS;
                }
            case MARKER_INT_16:
                {
                    int16_t val;
                    err = consume_network_int(val);
                    if (err != BoltError::SUCCESS) return err;
                    out_value = static_cast<int64_t>(val);
                    return BoltError::SUCCESS;
                }
            case MARKER_INT_32:
                {
                    int32_t val;
                    err = consume_network_int(val);
                    if (err != BoltError::SUCCESS) return err;
                    out_value = static_cast<int64_t>(val);
                    return BoltError::SUCCESS;
                }
            case MARKER_INT_64:
                {
                    int64_t val;
                    err = consume_network_int(val);
                    if (err != BoltError::SUCCESS) return err;
                    out_value = val;
                    return BoltError::SUCCESS;
                }
            default:
                set_error(BoltError::INVALID_ARGUMENT);  // Should not be reached
                return error_state_;
        }
    }

    BoltError PackStreamReader::read_string_data_into(std::string& out_string, uint32_t size) {
        if (has_error()) return error_state_;
        out_string.clear();  // Clear before use

        if (size == 0) {
            return BoltError::SUCCESS;
        }
        try {
            out_string.resize(size);  // Potential std::bad_alloc or std::length_error
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::length_error&) {  // String too long
            set_error(BoltError::DESERIALIZATION_ERROR);
            return error_state_;
        } catch (const std::exception&) {  // Catch any other std exceptions from resize
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }
        // Note: A truly no-exception build might make catch(...) more relevant if `new` returns nullptr.

        return consume_bytes(out_string.data(), size);  // consume_bytes sets error_state_ on failure
    }

    BoltError PackStreamReader::read_string_value(uint8_t marker, Value& out_value) {
        if (has_error()) return error_state_;
        uint32_t size = 0;
        BoltError err;

        if ((marker & 0xF0) == MARKER_TINY_STRING_BASE) {
            size = marker & 0x0F;
        } else {
            switch (marker) {
                case MARKER_STRING_8:
                    {
                        uint8_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                        break;
                    }
                case MARKER_STRING_16:
                    {
                        uint16_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                        break;
                    }
                case MARKER_STRING_32:
                    {
                        uint32_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                        break;
                    }
                default:
                    set_error(BoltError::INVALID_ARGUMENT);
                    return error_state_;
            }
        }

        std::string s_val;
        err = read_string_data_into(s_val, size);
        if (err != BoltError::SUCCESS) return err;

        try {
            out_value = std::move(s_val);  // std::string move ctor can technically throw (rarely, if allocator does)
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::exception&) {
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }
        return BoltError::SUCCESS;
    }

    BoltError PackStreamReader::read_list_elements_into(std::shared_ptr<BoltList>& list_sptr, uint32_t size) {
        if (has_error()) return error_state_;
        if (!list_sptr) {  // Should be pre-allocated by caller
            set_error(BoltError::INVALID_ARGUMENT);
            return error_state_;
        }

        if (current_recursion_depth_ >= MAX_RECURSION_DEPTH) {
            set_error(BoltError::RECURSION_DEPTH_EXCEEDED);
            return error_state_;
        }
        current_recursion_depth_++;

        BoltError err = BoltError::SUCCESS;
        try {
            list_sptr->elements.reserve(size);  // Potential std::bad_alloc
        } catch (const std::bad_alloc&) {
            current_recursion_depth_--;
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::length_error&) {
            current_recursion_depth_--;
            set_error(BoltError::DESERIALIZATION_ERROR);  // Size too large for vector
            return error_state_;
        } catch (const std::exception&) {
            current_recursion_depth_--;
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }

        for (uint32_t i = 0; i < size; ++i) {
            Value element;
            err = this->read(element);  // Recursive call
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                // error_state_ is already set by the recursive call to read()
                return error_state_;
            }
            try {
                list_sptr->elements.push_back(std::move(element));  // Value move ctor/vector push_back
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

    BoltError PackStreamReader::read_list_value(uint8_t marker, Value& out_value) {
        if (has_error()) return error_state_;
        uint32_t size = 0;
        BoltError err;

        if ((marker & 0xF0) == MARKER_TINY_LIST_BASE) {
            size = marker & 0x0F;
        } else {
            switch (marker) {
                case MARKER_LIST_8:
                    {
                        uint8_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                        break;
                    }
                case MARKER_LIST_16:
                    {
                        uint16_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                        break;
                    }
                case MARKER_LIST_32:
                    {
                        uint32_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                        break;
                    }
                default:
                    set_error(BoltError::INVALID_ARGUMENT);
                    return error_state_;
            }
        }

        std::shared_ptr<BoltList> list_sptr;
        try {
            list_sptr = std::make_shared<BoltList>();  // Potential std::bad_alloc
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::exception&) {
            set_error(BoltError::UNKNOWN_ERROR);  // Other errors from make_shared construction
            return error_state_;
        }
        if (!list_sptr) {  // Should not happen with make_shared unless exceptions truly disabled and new returns nullptr
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        }

        err = read_list_elements_into(list_sptr, size);
        if (err != BoltError::SUCCESS) return err;  // Error already set

        out_value = std::move(list_sptr);
        return BoltError::SUCCESS;
    }

    BoltError PackStreamReader::read_map_pairs_into(std::shared_ptr<BoltMap>& map_sptr, uint32_t size) {
        if (has_error()) return error_state_;
        if (!map_sptr) {
            set_error(BoltError::INVALID_ARGUMENT);
            return error_state_;
        }

        if (current_recursion_depth_ >= MAX_RECURSION_DEPTH) {
            set_error(BoltError::RECURSION_DEPTH_EXCEEDED);
            return error_state_;
        }
        current_recursion_depth_++;

        BoltError err;
        for (uint32_t i = 0; i < size; ++i) {
            Value key_val;
            err = this->read(key_val);  // Read map key
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;
            }

            std::string key_str;
            if (std::holds_alternative<std::string>(key_val)) {
                try {
                    // Move if possible, copy if necessary (Value holds string directly)
                    key_str = std::get<std::string>(std::move(key_val));
                } catch (const std::bad_alloc&) {
                    current_recursion_depth_--;
                    set_error(BoltError::OUT_OF_MEMORY);
                    return error_state_;
                } catch (const std::exception&) {
                    current_recursion_depth_--;
                    set_error(BoltError::UNKNOWN_ERROR);
                    return error_state_;
                }
            } else {
                current_recursion_depth_--;
                set_error(BoltError::INVALID_MESSAGE_FORMAT);  // Map key not a string
                return error_state_;
            }

            Value map_value;
            err = this->read(map_value);  // Read map value
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;
            }

            try {
                // map::emplace can throw (e.g. bad_alloc for node, or if Value copy/move throws)
                map_sptr->pairs.emplace(std::move(key_str), std::move(map_value));
            } catch (const std::bad_alloc&) {
                current_recursion_depth_--;
                set_error(BoltError::OUT_OF_MEMORY);
                return error_state_;
            } catch (const std::exception&) {  // Other exceptions from emplace or Value's move
                current_recursion_depth_--;
                set_error(BoltError::UNKNOWN_ERROR);
                return error_state_;
            }
        }
        current_recursion_depth_--;
        return BoltError::SUCCESS;
    }

    BoltError PackStreamReader::read_map_value(uint8_t marker, Value& out_value) {
        if (has_error()) return error_state_;
        uint32_t size = 0;
        BoltError err;

        if ((marker & 0xF0) == MARKER_TINY_MAP_BASE) {
            size = marker & 0x0F;
        } else {
            switch (marker) {
                case MARKER_MAP_8:
                    {
                        uint8_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                        break;
                    }
                case MARKER_MAP_16:
                    {
                        uint16_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                        break;
                    }
                case MARKER_MAP_32:
                    {
                        uint32_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                        break;
                    }
                default:
                    set_error(BoltError::INVALID_ARGUMENT);
                    return error_state_;
            }
        }

        std::shared_ptr<BoltMap> map_sptr;
        try {
            map_sptr = std::make_shared<BoltMap>();
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::exception&) {
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }
        if (!map_sptr) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        }

        err = read_map_pairs_into(map_sptr, size);
        if (err != BoltError::SUCCESS) return err;

        out_value = std::move(map_sptr);
        return BoltError::SUCCESS;
    }

    BoltError PackStreamReader::read_struct_fields_into(std::shared_ptr<PackStreamStructure>& struct_sptr, uint8_t tag, uint32_t size) {
        if (has_error()) return error_state_;
        if (!struct_sptr) {
            set_error(BoltError::INVALID_ARGUMENT);
            return error_state_;
        }

        struct_sptr->tag = tag;

        if (current_recursion_depth_ >= MAX_RECURSION_DEPTH) {
            set_error(BoltError::RECURSION_DEPTH_EXCEEDED);
            return error_state_;
        }
        current_recursion_depth_++;

        BoltError err;
        try {
            struct_sptr->fields.reserve(size);
        } catch (const std::bad_alloc&) {
            current_recursion_depth_--;
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::length_error&) {
            current_recursion_depth_--;
            set_error(BoltError::DESERIALIZATION_ERROR);
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

        if ((marker & 0xF0) == MARKER_TINY_STRUCT_BASE) {
            size = marker & 0x0F;
            err = consume_byte(tag);
            if (err != BoltError::SUCCESS) return err;
        } else {
            switch (marker) {
                case MARKER_STRUCT_8:
                    {
                        uint8_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                    }
                    err = consume_byte(tag);
                    if (err != BoltError::SUCCESS) return err;
                    break;
                case MARKER_STRUCT_16:
                    {
                        uint16_t s;
                        err = consume_network_int(s);
                        if (err != BoltError::SUCCESS) return err;
                        size = s;
                    }
                    err = consume_byte(tag);
                    if (err != BoltError::SUCCESS) return err;
                    break;
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
        if (!struct_sptr) {
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        }

        err = read_struct_fields_into(struct_sptr, tag, size);
        if (err != BoltError::SUCCESS) return err;

        out_value = std::move(struct_sptr);
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol