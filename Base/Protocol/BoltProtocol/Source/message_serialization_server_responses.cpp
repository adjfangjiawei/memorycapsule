#include <exception>  // For std::bad_alloc, std::bad_variant_access
#include <map>        // For SuccessMessageParams, FailureMessageParams
#include <memory>     // For std::shared_ptr
#include <variant>    // For std::holds_alternative, std::get
#include <vector>     // For RecordMessageParams

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"  // For declarations including deserialize_message_structure_prelude
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    // Forward declare the common helper if it's not pulled in via message_serialization.h
    // However, message_serialization.h should declare it if it's part of the public API of this component.
    // Assuming deserialize_message_structure_prelude is declared (even if defined elsewhere).

    BoltError deserialize_success_message(PackStreamReader& reader, SuccessMessageParams& out_params) {
        if (reader.has_error()) return reader.get_error();
        out_params.metadata.clear();  // Clear before use

        PackStreamStructure success_struct_contents;
        // SUCCESS message PSS has 1 field: a map of metadata.
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::SUCCESS, 1, 1, success_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;  // Propagate error (reader.error_state_ is already set by prelude)
        }

        // The single field must be a map.
        if (success_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(success_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        std::shared_ptr<BoltMap> metadata_map_sptr;
        try {
            metadata_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(success_struct_contents.fields[0]));
        } catch (const std::bad_variant_access&) {  // Defensive, should be caught by holds_alternative
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::exception&) {  // Other issues like bad_alloc from Value's move
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!metadata_map_sptr) {  // Null shared_ptr for the map field
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
        // FAILURE message PSS has 1 field: a map of metadata.
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
        // RECORD message PSS has 1 field: a list of values.
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

}  // namespace boltprotocol