#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError serialize_route_message(const RouteMessageParams& params, PackStreamWriter& writer, const versions::Version& negotiated_bolt_version) {
        if (writer.has_error()) return writer.get_error();

        if (negotiated_bolt_version < versions::Version(4, 3)) {
            writer.set_error(BoltError::SERIALIZATION_ERROR);
            return writer.get_error();
        }

        PackStreamStructure route_struct_obj;
        route_struct_obj.tag = static_cast<uint8_t>(MessageTag::ROUTE);

        std::shared_ptr<BoltMap> routing_table_context_map_sptr;
        std::shared_ptr<BoltList> bookmarks_list_sptr;

        try {
            routing_table_context_map_sptr = std::make_shared<BoltMap>();
            routing_table_context_map_sptr->pairs = params.routing_table_context;
            route_struct_obj.fields.emplace_back(Value(routing_table_context_map_sptr));

            bookmarks_list_sptr = std::make_shared<BoltList>();
            for (const auto& bookmark_str : params.bookmarks) {
                bookmarks_list_sptr->elements.emplace_back(Value(bookmark_str));
            }
            route_struct_obj.fields.emplace_back(Value(bookmarks_list_sptr));

            if (negotiated_bolt_version.major == 4 && negotiated_bolt_version.minor == 3) {  // Bolt 4.3
                if (params.db_name_for_v43.has_value()) {
                    route_struct_obj.fields.emplace_back(Value(params.db_name_for_v43.value()));
                } else {
                    route_struct_obj.fields.emplace_back(nullptr);
                }
            } else if (negotiated_bolt_version.major > 4 || (negotiated_bolt_version.major == 4 && negotiated_bolt_version.minor >= 4)) {  // Bolt 4.4+
                std::shared_ptr<BoltMap> extra_map_sptr = std::make_shared<BoltMap>();                                                     // Always create the map
                if (params.extra_for_v44_plus.has_value()) {
                    extra_map_sptr->pairs = params.extra_for_v44_plus.value();
                }
                // The 'extra' dictionary field is always present for Bolt 4.4+, even if empty.
                route_struct_obj.fields.emplace_back(Value(extra_map_sptr));
            }
            // No other cases, as we checked for version < 4.3 at the beginning.

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