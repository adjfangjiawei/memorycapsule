#include <exception>
#include <memory>  // For std::shared_ptr (though not strictly needed for RESET/GOODBYE fields)

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"  // For deserialize_message_structure_prelude
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    BoltError deserialize_reset_message_request(PackStreamReader& reader) {
        if (reader.has_error()) return reader.get_error();
        // RESET message PSS has 0 fields.
        PackStreamStructure reset_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::RESET, 0, 0, reset_struct_contents);
        // No fields to parse further from reset_struct_contents.fields.
        return err;
    }

    BoltError deserialize_goodbye_message_request(PackStreamReader& reader) {
        if (reader.has_error()) return reader.get_error();
        // GOODBYE message PSS has 0 fields.
        PackStreamStructure goodbye_struct_contents;
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::GOODBYE, 0, 0, goodbye_struct_contents);
        // No fields to parse further.
        return err;
    }

    // Add deserialize_route_request, deserialize_telemetry_request here in the future if needed.

}  // namespace boltprotocol