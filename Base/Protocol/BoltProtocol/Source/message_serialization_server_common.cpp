#include <exception>  // For std::bad_alloc, std::exception
#include <memory>     // For std::shared_ptr
#include <variant>    // For std::holds_alternative, std::get
#include <vector>     // For PackStreamStructure::fields

#include "boltprotocol/message_defs.h"           // For message structs, Value, MessageTag, BoltError
#include "boltprotocol/message_serialization.h"  // 主头文件，声明这些函数
#include "boltprotocol/packstream_reader.h"      // For PackStreamReader

namespace boltprotocol {

    // Helper to deserialize a structure from a reader and validate its basic properties
    // This function was originally in message_serialization_server.cpp
    BoltError deserialize_message_structure_prelude(PackStreamReader& reader, MessageTag expected_tag, size_t expected_fields_min, size_t expected_fields_max, PackStreamStructure& out_structure_contents) {
        if (reader.has_error()) return reader.get_error();

        Value raw_value;
        BoltError err = reader.read(raw_value);
        if (err != BoltError::SUCCESS) {
            // reader.read() should have set its internal error state.
            return err;
        }

        std::shared_ptr<PackStreamStructure> struct_sptr;
        if (std::holds_alternative<std::shared_ptr<PackStreamStructure>>(raw_value)) {
            try {
                struct_sptr = std::get<std::shared_ptr<PackStreamStructure>>(std::move(raw_value));
            } catch (const std::bad_variant_access&) {  // Should not happen due to holds_alternative
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            } catch (const std::exception&) {  // Other potential issues with std::get or Value move
                reader.set_error(BoltError::UNKNOWN_ERROR);
                return BoltError::UNKNOWN_ERROR;
            }
        } else {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        if (!struct_sptr) {  // Null shared_ptr received in Value
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        // Move the contents from the object pointed to by shared_ptr into out_structure_contents.
        try {
            out_structure_contents = std::move(*struct_sptr);  // PackStreamStructure move assignment
        } catch (const std::bad_alloc&) {                      // If vector/map move assignment allocates and fails
            reader.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (static_cast<MessageTag>(out_structure_contents.tag) != expected_tag) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);  // Tag mismatch
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        if (out_structure_contents.fields.size() < expected_fields_min || out_structure_contents.fields.size() > expected_fields_max) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);  // Field count mismatch
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        return BoltError::SUCCESS;
    }

    // peek_message_structure_header remains problematic and likely unused as discussed.
    // If it were to be implemented and was general, it could go here.
    // For now, it's commented out or returns an error in message_serialization.h
    /*
    BoltError peek_message_structure_header(PackStreamReader& reader, uint8_t& out_tag, uint32_t& out_fields_count) {
        // ... implementation ...
        return BoltError::UNKNOWN_ERROR; // Placeholder
    }
    */

}  // namespace boltprotocol