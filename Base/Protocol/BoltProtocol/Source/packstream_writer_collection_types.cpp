#include <exception>  // For std::bad_alloc
#include <limits>     // For std::numeric_limits
#include <map>
#include <memory>  // For std::shared_ptr (used in Value variant)
#include <string>  // For map keys
#include <vector>

#include "boltprotocol/message_defs.h"  // For BoltList, BoltMap, Value
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError PackStreamWriter::write_list_header_internal(uint32_t size) {
        if (has_error()) return error_state_;
        BoltError err = BoltError::SUCCESS;

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
            set_error(BoltError::SERIALIZATION_ERROR);  // List too large for PackStream size encoding
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
            return error_state_;  // Error already set
        }

        for (const auto& item : list_elements) {
            err = this->write(item);  // Recursive call to PackStreamWriter::write for each element
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
        BoltError err = BoltError::SUCCESS;

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
            return error_state_;
        }

        for (const auto& pair : map_pairs) {
            // Write key (must be string for PackStream maps)
            err = serialize_string_internal(pair.first);
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;  // Error already set by serialize_string_internal
            }
            // Write value (recursive call)
            err = this->write(pair.second);
            if (err != BoltError::SUCCESS) {
                current_recursion_depth_--;
                return error_state_;  // Error already set by recursive write()
            }
        }
        current_recursion_depth_--;
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol