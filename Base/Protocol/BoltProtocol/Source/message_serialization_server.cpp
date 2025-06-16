#include <exception>  // For std::bad_alloc (though less likely here, more in reader)
#include <map>
#include <memory>   // For std::shared_ptr
#include <variant>  // For std::holds_alternative, std::get
#include <vector>   // For PackStreamStructure::fields

#include "boltprotocol/message_defs.h"           // For message structs, Value, MessageTag, BoltError
#include "boltprotocol/message_serialization.h"  // Defines interfaces
#include "boltprotocol/packstream_reader.h"      // For PackStreamReader

namespace boltprotocol {

    // Helper to deserialize a structure from a reader and validate its basic properties
    BoltError deserialize_message_structure_prelude(PackStreamReader& reader, MessageTag expected_tag, size_t expected_fields_min, size_t expected_fields_max, PackStreamStructure& out_structure_contents) {
        if (reader.has_error()) return reader.get_error();

        Value raw_value;
        BoltError err = reader.read(raw_value);  // This now returns BoltError
        if (err != BoltError::SUCCESS) {
            // reader.read() should have set its internal error state.
            // We could propagate reader.get_error() or err. They should be the same.
            return err;
        }

        std::shared_ptr<PackStreamStructure> struct_sptr;
        if (std::holds_alternative<std::shared_ptr<PackStreamStructure>>(raw_value)) {
            // Value's get<>() for shared_ptr doesn't throw bad_variant_access if holds_alternative is true.
            // Move from variant. This itself shouldn't throw bad_alloc unless Value's move ctor does something very unusual.
            try {
                struct_sptr = std::get<std::shared_ptr<PackStreamStructure>>(std::move(raw_value));
            } catch (const std::bad_variant_access&) {                // Should not happen due to holds_alternative
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);  // Defensive
                return BoltError::INVALID_MESSAGE_FORMAT;
            } catch (const std::exception&) {  // Other potential issues with std::get or Value move
                reader.set_error(BoltError::UNKNOWN_ERROR);
                return BoltError::UNKNOWN_ERROR;
            }
        } else {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        if (!struct_sptr) {  // Null shared_ptr in Value
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        // Move the contents from the object pointed to by shared_ptr into out_structure_contents.
        // PackStreamStructure move assignment. This should generally not throw if members are well-behaved (vector/map move).
        try {
            out_structure_contents = std::move(*struct_sptr);
        } catch (const std::bad_alloc&) {  // If vector/map move assignment allocates and fails
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

    BoltError deserialize_success_message(PackStreamReader& reader, SuccessMessageParams& out_params) {
        if (reader.has_error()) return reader.get_error();
        out_params.metadata.clear();  // Clear before use

        PackStreamStructure success_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::SUCCESS, 1, 1, success_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;  // Propagate error (reader.error_state_ is already set)
        }

        if (success_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(success_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        std::shared_ptr<BoltMap> metadata_map_sptr;
        try {
            metadata_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(success_struct_contents.fields[0]));
        } catch (const std::bad_variant_access&) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!metadata_map_sptr) {  // Null shared_ptr
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        try {
            out_params.metadata = std::move(metadata_map_sptr->pairs);  // map move assignment
        } catch (const std::bad_alloc&) {
            reader.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        return BoltError::SUCCESS;
    }

    BoltError deserialize_failure_message(PackStreamReader& reader, FailureMessageParams& out_params) {
        if (reader.has_error()) return reader.get_error();
        out_params.metadata.clear();

        PackStreamStructure failure_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::FAILURE, 1, 1, failure_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }

        if (failure_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(failure_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        std::shared_ptr<BoltMap> metadata_map_sptr;
        try {
            metadata_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(failure_struct_contents.fields[0]));
        } catch (const std::bad_variant_access&) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!metadata_map_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        try {
            out_params.metadata = std::move(metadata_map_sptr->pairs);
        } catch (const std::bad_alloc&) {
            reader.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        return BoltError::SUCCESS;
    }

    BoltError deserialize_record_message(PackStreamReader& reader, RecordMessageParams& out_params) {
        if (reader.has_error()) return reader.get_error();
        out_params.fields.clear();

        PackStreamStructure record_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::RECORD, 1, 1, record_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }

        if (record_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltList>>(record_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        std::shared_ptr<BoltList> fields_list_sptr;
        try {
            fields_list_sptr = std::get<std::shared_ptr<BoltList>>(std::move(record_struct_contents.fields[0]));
        } catch (const std::bad_variant_access&) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!fields_list_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        try {
            out_params.fields = std::move(fields_list_sptr->elements);  // vector move assignment
        } catch (const std::bad_alloc&) {
            reader.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        return BoltError::SUCCESS;
    }

    BoltError deserialize_ignored_message(PackStreamReader& reader) {
        if (reader.has_error()) return reader.get_error();

        PackStreamStructure ignored_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::IGNORED, 0, 1, ignored_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }

        if (!ignored_struct_contents.fields.empty()) {
            if (!std::holds_alternative<std::shared_ptr<BoltMap>>(ignored_struct_contents.fields[0])) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
            // Metadata for IGNORED is optional and usually not used by client, so we don't try to get or move it.
            // If std::get was used, it would also need a try-catch.
        }
        return BoltError::SUCCESS;
    }

    BoltError peek_message_structure_header(PackStreamReader& /*reader*/, uint8_t& /*out_tag*/, uint32_t& /*out_fields_count*/) {
        // This function is problematic with the current reader design.
        // To peek a tag without fully consuming the message structure, the reader would need
        // more complex buffering or lookahead capabilities, or the ability to "unread" bytes.
        // Given the current sequential PackStreamReader, a true non-consuming peek is hard.
        // One could read the Value, get the tag, and then somehow "store" the Value for later full deserialization,
        // but that adds complexity.
        // For now, it's better that the dispatcher reads the full Value and then checks its tag.
        return BoltError::UNKNOWN_ERROR;  // Placeholder, likely not to be implemented this way.
    }

}  // namespace boltprotocol