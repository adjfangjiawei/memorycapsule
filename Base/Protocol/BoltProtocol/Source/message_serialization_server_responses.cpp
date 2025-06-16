#include <exception>
#include <map>
#include <memory>
#include <variant>
#include <vector>

#include "boltprotocol/detail/bolt_structure_helpers.h"  // For get_optional_list_string_from_map if used
#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    BoltError deserialize_success_message(PackStreamReader& reader, SuccessMessageParams& out_params) {
        if (reader.has_error()) return reader.get_error();
        out_params.metadata.clear();

        PackStreamStructure success_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::SUCCESS, 1, 1, success_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
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

        // Example of how an upper layer (e.g., ClientSession) might use this:
        // After calling deserialize_success_message for a HELLO response:
        // if (auto patch_list_val = boltprotocol::detail::get_optional_list_string_from_map(boltprotocol::BoltMap{out_params.metadata}, "patch_bolt")) {
        //    session.agreed_patches = patch_list_val.value();
        //    for(const auto& patch : session.agreed_patches) {
        //        if (patch == "utc") session.utc_patch_active_for_4_4 = true;
        //    }
        // }
        // This logic belongs in the consuming code, not the generic deserializer.

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
            out_params.fields = std::move(fields_list_sptr->elements);
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
        }

        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol