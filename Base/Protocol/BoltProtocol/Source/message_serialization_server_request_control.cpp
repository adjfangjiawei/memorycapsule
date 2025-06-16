#include <exception>
#include <map>
#include <memory>
#include <variant>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    namespace {
        std::optional<int64_t> get_optional_int64_from_map(const BoltMap& map, const std::string& key) {
            auto it = map.pairs.find(key);
            if (it != map.pairs.end() && std::holds_alternative<int64_t>(it->second)) {
                try {
                    return std::get<int64_t>(it->second);
                } catch (...) {
                }
            }
            return std::nullopt;
        }
    }  // namespace

    BoltError deserialize_reset_message_request(PackStreamReader& reader) {
        if (reader.has_error()) return reader.get_error();
        // RESET message PSS (Bolt 1+) has 0 fields.
        PackStreamStructure reset_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::RESET, 0, 0, reset_struct_contents);
        // No fields to parse further from reset_struct_contents.fields.
        return err;
    }

    BoltError deserialize_goodbye_message_request(PackStreamReader& reader) {
        if (reader.has_error()) return reader.get_error();
        // GOODBYE message PSS (Bolt 3+) has 0 fields.
        PackStreamStructure goodbye_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::GOODBYE, 0, 0, goodbye_struct_contents);
        // No fields to parse further.
        return err;
    }

    BoltError deserialize_telemetry_message_request(PackStreamReader& reader, TelemetryMessageParams& out_params) {
        if (reader.has_error()) return reader.get_error();
        out_params.metadata.clear();

        PackStreamStructure telemetry_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::TELEMETRY, 1, 1, telemetry_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }

        if (telemetry_struct_contents.fields.empty() || !std::holds_alternative<std::shared_ptr<BoltMap>>(telemetry_struct_contents.fields[0])) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        auto metadata_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(telemetry_struct_contents.fields[0]));
        if (!metadata_map_sptr) {
            reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        try {
            out_params.metadata = std::move(metadata_map_sptr->pairs);
        } catch (const std::bad_alloc&) {
            reader.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (...) {
            reader.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        auto api_val_it = out_params.metadata.find("api");
        if (api_val_it == out_params.metadata.end() || !std::holds_alternative<int64_t>(api_val_it->second)) {
            // Specification: "unless it sends an invalid value for the api field, which results in a FAILURE response."
            // This deserializer can flag it, actual FAILURE is up to server logic.
            // For now, successful deserialization of the structure, content validation is next step.
            // reader.set_error(BoltError::INVALID_MESSAGE_FORMAT); // Or a specific "TELEMETRY_INVALID_API_FIELD"
            // return reader.get_error();
        }
        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol