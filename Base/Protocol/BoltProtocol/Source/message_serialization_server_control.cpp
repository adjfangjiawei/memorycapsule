#include <exception>  // For std::bad_alloc, std::bad_variant_access
#include <memory>     // For std::shared_ptr
#include <variant>    // For std::holds_alternative, std::get (though not strictly used for get if field is optional)

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"

namespace boltprotocol {

    BoltError deserialize_ignored_message(PackStreamReader& reader) {
        if (reader.has_error()) return reader.get_error();

        PackStreamStructure ignored_struct_contents;
        // IGNORED message PSS has 0 or 1 field. If 1, it's a map (usually for future use or diagnostics).
        // The spec typically shows IGNORED {}, meaning 0 fields in its PSS, or IGNORED {<metadata_map>}.
        // A PSS with 0 fields is valid. A PSS with 1 field (the map) is also valid.
        // So, expected_fields_min = 0, expected_fields_max = 1.
        BoltError err = deserialize_message_structure_prelude(reader, MessageTag::IGNORED, 0, 1, ignored_struct_contents);
        if (err != BoltError::SUCCESS) {
            return err;
        }

        // If there is a field, it must be a map.
        // Client usually doesn't need to parse this map, so we just validate its presence and type if it exists.
        if (!ignored_struct_contents.fields.empty()) {  // Field is present
            if (!std::holds_alternative<std::shared_ptr<BoltMap>>(ignored_struct_contents.fields[0])) {
                reader.set_error(BoltError::INVALID_MESSAGE_FORMAT);
                return BoltError::INVALID_MESSAGE_FORMAT;
            }
            // We don't need to extract the map's content for IGNORED typically.
            // std::shared_ptr<BoltMap> metadata_map_sptr = std::get<std::shared_ptr<BoltMap>>(ignored_struct_contents.fields[0]);
            // if (!metadata_map_sptr) { /* This would be an error if the field is present but map pointer is null */ }
        }
        // If fields is empty, it's a valid IGNORED {} message, nothing more to do.

        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol