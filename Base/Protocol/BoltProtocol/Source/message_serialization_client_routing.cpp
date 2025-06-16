#include <exception>  // For std::bad_alloc, std::exception
#include <map>
#include <memory>    // For std::make_shared, std::shared_ptr
#include <optional>  // For std::optional in RouteMessageParams
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"  // For function declarations
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    BoltError serialize_route_message(const RouteMessageParams& params, PackStreamWriter& writer, const versions::Version& negotiated_bolt_version) {
        if (writer.has_error()) return writer.get_error();

        PackStreamStructure route_struct_obj;  // Create on stack
        route_struct_obj.tag = static_cast<uint8_t>(MessageTag::ROUTE);

        std::shared_ptr<BoltMap> context_map_sptr;
        std::shared_ptr<BoltList> bookmarks_list_sptr;

        try {
            // Field 1: Routing Context (Map)
            context_map_sptr = std::make_shared<BoltMap>();
            // The client application is responsible for populating this map correctly.
            // For ROUTE V2 (Bolt 5.1+ usually), "db" and "imp_user" might be included here.
            context_map_sptr->pairs = params.routing_context;
            route_struct_obj.fields.emplace_back(Value(context_map_sptr));

            // Field 2: Bookmarks (List of Strings)
            bookmarks_list_sptr = std::make_shared<BoltList>();
            for (const auto& bookmark_str : params.bookmarks) {
                bookmarks_list_sptr->elements.emplace_back(Value(bookmark_str));
            }
            route_struct_obj.fields.emplace_back(Value(bookmarks_list_sptr));

            // Add additional top-level fields based on the negotiated Bolt version.
            // This structure aligns with how ROUTE messages evolved:
            // - Bolt < 4.3: 2 fields (context, bookmarks)
            // - Bolt 4.3, 4.4: 3 fields (context, bookmarks, db_name_or_null)
            // - Bolt >= 5.0: 4 fields (context, bookmarks, db_name_or_null, imp_user_or_null)
            // The server might prioritize values from routing_context if they conflict with these top-level fields,
            // especially for ROUTE V2 aware servers.

            if (negotiated_bolt_version.major == 4 && negotiated_bolt_version.minor >= 3) {
                // Bolt 4.3 or 4.4: Add db_name as 3rd field
                if (params.db_name.has_value()) {
                    route_struct_obj.fields.emplace_back(Value(params.db_name.value()));
                } else {
                    route_struct_obj.fields.emplace_back(nullptr);  // Explicit PackStream NULL
                }
            } else if (negotiated_bolt_version.major >= 5) {
                // Bolt 5.0+: Add db_name as 3rd field and imp_user as 4th field

                // Field 3: Database Name (string or null)
                if (params.db_name.has_value()) {
                    route_struct_obj.fields.emplace_back(Value(params.db_name.value()));
                } else {
                    route_struct_obj.fields.emplace_back(nullptr);
                }

                // Field 4: Impersonated User (string or null)
                if (params.impersonated_user.has_value()) {
                    route_struct_obj.fields.emplace_back(Value(params.impersonated_user.value()));
                } else {
                    route_struct_obj.fields.emplace_back(nullptr);
                }
            } else {
                // Bolt < 4.3: Only 2 fields.
                // If db_name or impersonated_user were provided in params for these older versions,
                // they are effectively ignored as they won't be serialized as top-level PSS fields.
                // A stricter implementation might raise an error here if they are present.
                if (params.db_name.has_value()) {
                    // Log warning or handle as error if strict adherence is needed for older versions
                    // For now, we simply don't serialize it.
                }
                if (params.impersonated_user.has_value()) {
                    // Log warning or handle as error
                }
            }

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {  // Catch other potential exceptions from map/list/value operations
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_route_message (fields prep): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        // Convert the stack-based PSS object to a shared_ptr for the writer
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(route_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {  // Other exceptions from make_shared or PSS move ctor
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_route_message (pss make_shared): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_sptr) {  // Should typically be caught by bad_alloc if make_shared fails
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }

        return writer.write(Value(std::move(pss_sptr)));
    }

    // Note: serialize_telemetry_message can be in its own message_serialization_client_telemetry.cpp
    // or kept here if preferred for fewer files. For this submission, it's included here.
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
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(telemetry_struct_obj));
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

}  // namespace boltprotocol