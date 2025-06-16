#include <exception>  // For std::bad_alloc, std::exception
#include <map>
#include <memory>  // For std::make_shared, std::shared_ptr
#include <vector>  // For PackStreamStructure fields

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"  // For the function declaration
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError serialize_telemetry_message(const TelemetryMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure telemetry_struct_obj;                                // Create on stack
        telemetry_struct_obj.tag = static_cast<uint8_t>(MessageTag::TELEMETRY);  // Tag 0x54

        std::shared_ptr<BoltMap> metadata_map_sptr;
        try {
            metadata_map_sptr = std::make_shared<BoltMap>();
            // Copy the metadata from params. Telemetry typically has specific keys like "api".
            // The caller is responsible for ensuring params.metadata is correctly populated.
            metadata_map_sptr->pairs = params.metadata;

            // The TELEMETRY message PackStreamStructure has one field: the metadata map.
            telemetry_struct_obj.fields.emplace_back(Value(metadata_map_sptr));

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {  // Catch other potential exceptions from map copy or emplace_back
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // Consider logging e_std.what() for debugging if needed
            return BoltError::UNKNOWN_ERROR;
        }

        // Now, convert the stack-based PSS object to a shared_ptr to pass to writer.write()
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(telemetry_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {  // Other exceptions from make_shared or PSS move ctor
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_sptr) {                                 // Should typically be caught by bad_alloc if make_shared fails
            writer.set_error(BoltError::OUT_OF_MEMORY);  // Or UNKNOWN_ERROR if make_shared could return null without exception
            return BoltError::OUT_OF_MEMORY;
        }

        return writer.write(Value(std::move(pss_sptr)));
    }

}  // namespace boltprotocol