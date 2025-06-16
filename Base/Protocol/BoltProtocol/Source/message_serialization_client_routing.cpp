#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"  // Includes bolt_errors_versions.h for versions::Version
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError serialize_route_message(const RouteMessageParams& params, PackStreamWriter& writer, const versions::Version& negotiated_bolt_version) {
        if (writer.has_error()) return writer.get_error();

        // ROUTE message was introduced in Bolt 4.3
        if (negotiated_bolt_version < versions::Version(4, 3)) {
            writer.set_error(BoltError::SERIALIZATION_ERROR);  // Or UNSUPPORTED_PROTOCOL_VERSION for this message
            // std::cerr << "Error: ROUTE message is not supported in Bolt version "
            //           << (int)negotiated_bolt_version.major << "." << (int)negotiated_bolt_version.minor << std::endl;
            return writer.get_error();
        }

        PackStreamStructure route_struct_obj;
        route_struct_obj.tag = static_cast<uint8_t>(MessageTag::ROUTE);

        std::shared_ptr<BoltMap> routing_table_context_map_sptr;
        std::shared_ptr<BoltList> bookmarks_list_sptr;

        try {
            // Field 1: routing::Dictionary (the routing context from client, e.g. initial address)
            routing_table_context_map_sptr = std::make_shared<BoltMap>();
            routing_table_context_map_sptr->pairs = params.routing_table_context;
            route_struct_obj.fields.emplace_back(Value(routing_table_context_map_sptr));

            // Field 2: bookmarks::List<String>
            bookmarks_list_sptr = std::make_shared<BoltList>();
            for (const auto& bookmark_str : params.bookmarks) {
                bookmarks_list_sptr->elements.emplace_back(Value(bookmark_str));
            }
            route_struct_obj.fields.emplace_back(Value(bookmarks_list_sptr));

            // Field 3: Varies by version
            if (negotiated_bolt_version.major == 4 && negotiated_bolt_version.minor == 3) {  // Bolt 4.3
                // Third field is db::String (or null)
                if (params.db_name_for_v43.has_value()) {
                    route_struct_obj.fields.emplace_back(Value(params.db_name_for_v43.value()));
                } else {
                    route_struct_obj.fields.emplace_back(nullptr);  // Explicit PackStream NULL
                }
                // imp_user not applicable as a top-level field for 4.3 ROUTE PSS.
                if (params.extra_for_v44_plus.has_value()) {
                    // Warning: extra_for_v44_plus provided but serializing for 4.3
                }
            } else if (negotiated_bolt_version.major > 4 || (negotiated_bolt_version.major == 4 && negotiated_bolt_version.minor >= 4)) {  // Bolt 4.4+
                // Third field is extra::Dictionary(db::String, imp_user::String)
                std::shared_ptr<BoltMap> extra_map_sptr = std::make_shared<BoltMap>();
                if (params.extra_for_v44_plus.has_value()) {
                    extra_map_sptr->pairs = params.extra_for_v44_plus.value();
                }
                // Ensure extra map is always sent, even if empty, as per spec for 4.4+ (field is extra::Dictionary)
                route_struct_obj.fields.emplace_back(Value(extra_map_sptr));

                if (params.db_name_for_v43.has_value()) {
                    // Warning: db_name_for_v43 provided but serializing for 4.4+ (should be in extra_for_v44_plus)
                }
            }
            // Note: Bolt 5.0+ ROUTE PSS is also 3 fields like 4.4+, where the 3rd is an "extra" map.
            // The content of routing_table_context and the extra map for 5.0+ might differ semantically (ROUTE V2)
            // but the PSS structure for ROUTE message itself (tag 0x66) is 3 fields for 4.4+.
            // The old 4-field logic was based on a misinterpretation or a different message structure.
            // The spec for ROUTE message explicitly states 3 fields for 4.3 (context, bookmarks, db)
            // and 3 fields for 4.4+ (context, bookmarks, extra_map).

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(route_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }

        return writer.write(Value(std::move(pss_sptr)));
    }

    // serialize_telemetry_message implementation remains here
    BoltError serialize_telemetry_message(const TelemetryMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure telemetry_struct_obj;
        telemetry_struct_obj.tag = static_cast<uint8_t>(MessageTag::TELEMETRY);

        std::shared_ptr<BoltMap> metadata_map_sptr;
        try {
            metadata_map_sptr = std::make_shared<BoltMap>();
            metadata_map_sptr->pairs = params.metadata;
            telemetry_struct_obj.fields.emplace_back(Value(metadata_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(telemetry_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception&) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

}  // namespace boltprotocol