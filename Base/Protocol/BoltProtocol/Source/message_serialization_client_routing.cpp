#include <exception>  // For std::bad_alloc, std::exception
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

        PackStreamStructure route_struct_obj;  // On stack
        route_struct_obj.tag = static_cast<uint8_t>(MessageTag::ROUTE);

        std::shared_ptr<BoltMap> context_map_sptr;
        std::shared_ptr<BoltList> bookmarks_list_sptr;

        try {
            // Field 1: Routing Context (Map)
            context_map_sptr = std::make_shared<BoltMap>();
            context_map_sptr->pairs = params.routing_context;  // map copy
            route_struct_obj.fields.emplace_back(Value(context_map_sptr));

            // Field 2: Bookmarks (List of Strings)
            bookmarks_list_sptr = std::make_shared<BoltList>();
            for (const auto& bookmark_str : params.bookmarks) {
                bookmarks_list_sptr->elements.emplace_back(Value(bookmark_str));
            }
            route_struct_obj.fields.emplace_back(Value(bookmarks_list_sptr));

            // Field 3: Database Name (optional, depends on Bolt version)
            // For Bolt 4.3+ (ROUTE V1) or 5.1+ (ROUTE V2, where it's part of routing_context map usually,
            // but older ROUTE messages might have it as a separate field).
            // The ROUTE message structure changed significantly with Bolt 4.3 (ROUTE) and 5.1 (ROUTE V2).
            // The original `RouteMessageParams` and this serialization function appear to target
            // older ROUTE or a specific interpretation.
            // For Bolt >= 4.3, db name is the third parameter.
            // For Bolt >= 5.0 (often implies ROUTE v2), db name and imp_user are often in the routing_context map.
            // This function as written adds db_name and imp_user as separate fields if version conditions met.
            // Let's stick to the existing logic which adds them as separate fields based on version.

            bool db_field_present = (negotiated_bolt_version.major > 4 || (negotiated_bolt_version.major == 4 && negotiated_bolt_version.minor >= 3));
            bool imp_user_field_present = (negotiated_bolt_version.major >= 5);  // Typically, Bolt 5.0+

            if (db_field_present) {
                if (params.db_name.has_value()) {
                    route_struct_obj.fields.emplace_back(Value(params.db_name.value()));
                } else {
                    route_struct_obj.fields.emplace_back(nullptr);  // Explicit PackStream NULL
                }
            } else if (params.db_name.has_value() && !db_field_present) {
                // Trying to send db_name when version doesn't support it as a separate field in ROUTE.
                writer.set_error(BoltError::SERIALIZATION_ERROR);
                // std::cerr << "Error: db_name provided for ROUTE with Bolt version " << ... << " which does not support it as separate field." << std::endl;
                return BoltError::SERIALIZATION_ERROR;
            }

            if (imp_user_field_present) {
                // Ensure db_name field (even if null) is present if imp_user is being added for Bolt 5.0+
                // The logic: if db_field_present is false, but imp_user_field_present is true, this implies an issue
                // because imp_user is typically the 4th field after db_name (3rd field).
                // However, if the spec for a version says imp_user is present but db_name is not,
                // the current sequential addition is okay. Assuming spec means db_name slot is implicitly null.
                // The structure is (context, bookmarks, [db_name_or_null], [imp_user_or_null])
                // If db_field_present is false and imp_user_field_present is true, we need to ensure the slot for db_name is filled with null
                // if the version implies imp_user is the 4th field.
                // The current code adds fields based on flags. If db_field_present is false, no field is added for db.
                // Then if imp_user_field_present is true, it becomes the 3rd field. This might be incorrect for versions
                // like 5.0 if it expects db_name to always be the 3rd slot.
                // Re-evaluating based on specification:
                // Bolt 4.0-4.2: (map, list)
                // Bolt 4.3-4.4: (map, list, string|null)
                // Bolt 5.0: (map, list, string|null, string|null) -- this means 4 fields
                // So if `imp_user_field_present` is true (Bolt 5.0+), `db_field_present` MUST also be true.
                // If `imp_user_field_present` is true but `db_field_present` was calculated as false, this is a logic contradiction
                // based on how Bolt versions introduced these fields.
                // The current flags `db_field_present` and `imp_user_field_present` should be correct.
                // If `imp_user_field_present` is true, then `db_field_present` should also be true.

                if (!db_field_present && imp_user_field_present) {
                    // This case suggests a protocol version where impersonated user exists but db name as 3rd field does not.
                    // This seems unlikely for Bolt versions. Assuming if imp_user is a separate field, db_name is too.
                    // Let's ensure the db_name slot is filled if we are adding imp_user.
                    // This means if imp_user_field_present is true, we must have added the db_name field (even if null).
                    // The current logic of adding based on db_field_present first, then imp_user_field_present, is correct.
                }

                if (params.impersonated_user.has_value()) {
                    route_struct_obj.fields.emplace_back(Value(params.impersonated_user.value()));
                } else {
                    route_struct_obj.fields.emplace_back(nullptr);  // Explicit PackStream NULL
                }
            } else if (params.impersonated_user.has_value() && !imp_user_field_present) {
                writer.set_error(BoltError::SERIALIZATION_ERROR);
                // std::cerr << "Error: impersonated_user provided for ROUTE with Bolt version " << ... << " which does not support it as separate field." << std::endl;
                return BoltError::SERIALIZATION_ERROR;
            }

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_route_message (fields prep): " << e_std.what() << std::endl;
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
            // std::cerr << "Std exception (make_shared PSS) in serialize_route_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }

        return writer.write(Value(std::move(pss_sptr)));
    }

    // TODO: BoltError serialize_telemetry_message(const TelemetryMessageParams& params, PackStreamWriter& writer);

}  // namespace boltprotocol