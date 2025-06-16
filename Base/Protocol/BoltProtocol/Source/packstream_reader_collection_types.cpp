#include <exception>  // For std::bad_alloc, std::length_error, std::bad_variant_access
#include <map>
#include <memory>  // For std::shared_ptr, std::make_shared
#include <string>  // For map keys
#include <vector>

#include "boltprotocol/message_defs.h"  // For BoltList, BoltMap, Value
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    // Helper to read List elements into a pre-allocated BoltList shared_ptr
    BoltError PackStreamReader::read_list_elements_into(std::shared_ptr<BoltList>& list_sptr, uint32_t size) {
        if (has_error()) return error_state_;
        if (!list_sptr) {  // Should be allocated by caller (read_list_value)
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
            list_sptr->elements.reserve(size);  // Potential std::bad_alloc or std::length_error
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
            err = this->read(element);  // Recursive call to PackStreamReader::read for each element
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                // error_state_ is already set by the recursive call
                return error_state_;
            }
            try {
                list_sptr->elements.push_back(std::move(element));  // Value move ctor/vector push_back
            } catch (const std::bad_alloc&) {
                current_recursion_depth_--;
                set_error(BoltError::OUT_OF_MEMORY);
                return error_state_;
            } catch (const std::exception&) {  // Other potential exceptions from push_back or Value move
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

        if ((marker & 0xF0) == MARKER_TINY_LIST_BASE) {  // Tiny List (0x90 to 0x9F)
            size = marker & 0x0F;
        } else {
            switch (marker) {
                case MARKER_LIST_8:
                    {
                        uint8_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;
                        break;
                    }
                case MARKER_LIST_16:
                    {
                        uint16_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;
                        break;
                    }
                case MARKER_LIST_32:
                    {
                        uint32_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;
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
        } catch (const std::exception&) {  // Other errors from make_shared construction
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }
        // No need to check if (!list_sptr) because make_shared throws on failure.

        err = read_list_elements_into(list_sptr, size);
        if (err != BoltError::SUCCESS) {
            // error_state_ already set
            return error_state_;
        }

        try {
            out_value = std::move(list_sptr);  // Move shared_ptr into variant
        } catch (const std::bad_alloc&) {      // Variant assignment can allocate.
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        } catch (const std::exception&) {
            set_error(BoltError::UNKNOWN_ERROR);
            return error_state_;
        }
        return BoltError::SUCCESS;
    }

    // Helper to read Map key-value pairs into a pre-allocated BoltMap shared_ptr
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
            Value key_as_value;
            err = this->read(key_as_value);  // Read map key
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;
            }

            std::string key_str;
            if (std::holds_alternative<std::string>(key_as_value)) {
                try {
                    // std::get for rvalue variant returns rvalue ref or throws if type mismatch / bad state
                    key_str = std::get<std::string>(std::move(key_as_value));
                } catch (const std::bad_variant_access&) {  // Should not happen due to holds_alternative
                    current_recursion_depth_--;
                    set_error(BoltError::INVALID_MESSAGE_FORMAT);
                    return error_state_;
                } catch (const std::bad_alloc&) {  // string move assignment/ctor
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
                set_error(BoltError::INVALID_MESSAGE_FORMAT);  // Map key must be a string
                return error_state_;
            }

            Value map_value_element;
            err = this->read(map_value_element);  // Read map value
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;
            }

            try {
                // map::emplace can throw (e.g. bad_alloc for node, or if Value copy/move throws)
                map_sptr->pairs.emplace(std::move(key_str), std::move(map_value_element));
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

        if ((marker & 0xF0) == MARKER_TINY_MAP_BASE) {  // Tiny Map (0xA0 to 0xAF)
            size = marker & 0x0F;
        } else {
            switch (marker) {
                case MARKER_MAP_8:
                    {
                        uint8_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;
                        break;
                    }
                case MARKER_MAP_16:
                    {
                        uint16_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;
                        break;
                    }
                case MARKER_MAP_32:
                    {
                        uint32_t s_len;
                        err = consume_network_int(s_len);
                        if (err != BoltError::SUCCESS) return err;
                        size = s_len;
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

        err = read_map_pairs_into(map_sptr, size);
        if (err != BoltError::SUCCESS) return err;

        try {
            out_value = std::move(map_sptr);
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