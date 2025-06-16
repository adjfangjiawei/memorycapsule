#include <exception>  // For std::bad_alloc
#include <limits>     // For std::numeric_limits (for STRUCT_16 max size)
#include <memory>     // For std::shared_ptr (used in Value variant)
#include <vector>

#include "boltprotocol/message_defs.h"  // For PackStreamStructure, Value
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError PackStreamWriter::write_struct_header_internal(uint8_t tag, uint32_t size) {
        if (has_error()) return error_state_;
        BoltError err = BoltError::SUCCESS;

        if (size <= 0x0F) {  // Tiny Struct (0xB0 to 0xBF)
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
            // PackStream v1 (which Bolt uses) does not define STRUCT_32.
            // Maximum number of fields for a structure is 65535 (0xFFFF).
            set_error(BoltError::SERIALIZATION_ERROR);  // Structure too large for PackStream v1 encoding
            return error_state_;
        }
        return err;
    }

    BoltError PackStreamWriter::serialize_structure_internal(const PackStreamStructure& struct_data) {
        if (has_error()) return error_state_;

        // Max fields for a PackStream structure (STRUCT_16 limit)
        if (struct_data.fields.size() > std::numeric_limits<uint16_t>::max()) {  // 65535
            set_error(BoltError::SERIALIZATION_ERROR);                           // Structure too large
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
            return error_state_;
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