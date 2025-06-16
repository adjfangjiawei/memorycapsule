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

    BoltError serialize_hello_message(const HelloMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        PackStreamStructure pss_hello_obj;
        pss_hello_obj.tag = static_cast<uint8_t>(MessageTag::HELLO);
        std::shared_ptr<BoltMap> map_for_field_sptr;
        try {
            map_for_field_sptr = std::make_shared<BoltMap>();
            // Directly assign if extra_auth_tokens is already map<string, Value>
            // Otherwise, iterate and emplace if types differ or deep copy needed.
            map_for_field_sptr->pairs = params.extra_auth_tokens;
            Value map_value_as_field(map_for_field_sptr);
            pss_hello_obj.fields.emplace_back(std::move(map_value_as_field));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {  // Catch other potential exceptions from map copy/emplace
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_hello_message (map prep): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pss_hello_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_hello_message (pss make_shared): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_sptr) {  // Should be redundant if make_shared throws or bad_alloc caught
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_run_message(const RunMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        PackStreamStructure run_struct_obj;  // On stack
        run_struct_obj.tag = static_cast<uint8_t>(MessageTag::RUN);

        std::shared_ptr<BoltMap> parameters_map_sptr;
        std::shared_ptr<BoltMap> extra_metadata_map_sptr;

        try {
            // Field 1: Cypher query (string)
            run_struct_obj.fields.emplace_back(Value(params.cypher_query));  // string to Value can throw (bad_alloc)

            // Field 2: Parameters map
            parameters_map_sptr = std::make_shared<BoltMap>();
            parameters_map_sptr->pairs = params.parameters;                  // map copy
            run_struct_obj.fields.emplace_back(Value(parameters_map_sptr));  // Value copies shared_ptr

            // Field 3: Extra metadata map
            extra_metadata_map_sptr = std::make_shared<BoltMap>();
            extra_metadata_map_sptr->pairs = params.extra_metadata;              // map copy
            run_struct_obj.fields.emplace_back(Value(extra_metadata_map_sptr));  // Value copies shared_ptr

        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_run_message (fields prep): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(run_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_run_message (pss make_shared): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }

        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_pull_message(const PullMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        PackStreamStructure pull_struct_obj;
        pull_struct_obj.tag = static_cast<uint8_t>(MessageTag::PULL);
        std::shared_ptr<BoltMap> extra_map_sptr;

        try {
            extra_map_sptr = std::make_shared<BoltMap>();
            if (params.n.has_value()) {
                // Value constructor for int64_t is trivial, emplace can throw bad_alloc for map node
                extra_map_sptr->pairs.emplace("n", Value(params.n.value()));
            }
            if (params.qid.has_value()) {
                extra_map_sptr->pairs.emplace("qid", Value(params.qid.value()));
            }
            // PULL message structure is: PULL {"n": N, "qid": QID}
            // The map itself is the single field.
            pull_struct_obj.fields.emplace_back(Value(extra_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_pull_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pull_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_pull_message (pss make_shared): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }

        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_discard_message(const DiscardMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        PackStreamStructure discard_struct_obj;
        discard_struct_obj.tag = static_cast<uint8_t>(MessageTag::DISCARD);
        std::shared_ptr<BoltMap> extra_map_sptr;
        try {
            extra_map_sptr = std::make_shared<BoltMap>();
            if (params.n.has_value()) {
                extra_map_sptr->pairs.emplace("n", Value(params.n.value()));
            }
            if (params.qid.has_value()) {
                extra_map_sptr->pairs.emplace("qid", Value(params.qid.value()));
            }
            discard_struct_obj.fields.emplace_back(Value(extra_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_discard_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(discard_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_discard_message (pss make_shared): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }

        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_goodbye_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::GOODBYE);
            // No fields for GOODBYE
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_goodbye_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_reset_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::RESET);
            // No fields for RESET
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_reset_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_begin_message(const BeginMessageParams& params, PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        PackStreamStructure begin_struct_obj;  // On stack
        begin_struct_obj.tag = static_cast<uint8_t>(MessageTag::BEGIN);
        std::shared_ptr<BoltMap> extra_map_sptr;

        try {
            extra_map_sptr = std::make_shared<BoltMap>();
            extra_map_sptr->pairs = params.extra;  // map copy
            begin_struct_obj.fields.emplace_back(Value(extra_map_sptr));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_begin_message (map prep): " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(begin_struct_obj));
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception (make_shared PSS) in serialize_begin_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_commit_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::COMMIT);
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_commit_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

    BoltError serialize_rollback_message(PackStreamWriter& writer) {
        if (writer.has_error()) return writer.get_error();
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>();
            pss_sptr->tag = static_cast<uint8_t>(MessageTag::ROLLBACK);
        } catch (const std::bad_alloc&) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            writer.set_error(BoltError::UNKNOWN_ERROR);
            // std::cerr << "Std exception in serialize_rollback_message: " << e_std.what() << std::endl;
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        return writer.write(Value(std::move(pss_sptr)));
    }

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
                // Value constructor for string can throw bad_alloc, vector emplace_back can throw bad_alloc
                bookmarks_list_sptr->elements.emplace_back(Value(bookmark_str));
            }
            route_struct_obj.fields.emplace_back(Value(bookmarks_list_sptr));

            // Determine which optional fields to include based on Bolt version
            bool db_field_required_by_version = (negotiated_bolt_version.major > 4 || (negotiated_bolt_version.major == 4 && negotiated_bolt_version.minor >= 3));
            bool imp_user_field_required_by_version = (negotiated_bolt_version.major >= 5);

            if (db_field_required_by_version) {
                if (params.db_name.has_value()) {
                    route_struct_obj.fields.emplace_back(Value(params.db_name.value()));
                } else {
                    route_struct_obj.fields.emplace_back(nullptr);  // Explicit PackStream NULL
                }
            } else if (params.db_name.has_value()) {
                // Client trying to send db_name when version doesn't support it in ROUTE.
                writer.set_error(BoltError::SERIALIZATION_ERROR);
                // std::cerr << "Error: db_name provided for ROUTE with Bolt version " << ... << " which does not support it." << std::endl; // Optional log
                return BoltError::SERIALIZATION_ERROR;
            }

            if (imp_user_field_required_by_version) {
                // If db field was not required by version but imp_user is, the spec implies db field slot is still there (as null).
                // Our current logic correctly handles this: if db_field_required_by_version is false, nothing is added for db.
                // If then imp_user_field_required_by_version is true, it's added as the next field.
                // This means the number of fields for ROUTE varies (2, 3, or 4).
                // V4.0-4.2: (map, list) - 2 fields
                // V4.3-4.4: (map, list, db_or_null) - 3 fields
                // V5.0+:    (map, list, db_or_null, imp_user_or_null) - 4 fields
                // The logic needs to ensure that if imp_user is present for V5.0+, the db field (even if null) is also serialized
                // if the version is >= 4.3.
                // The current structure of adding fields sequentially based on flags is correct.

                if (params.impersonated_user.has_value()) {
                    route_struct_obj.fields.emplace_back(Value(params.impersonated_user.value()));
                } else {
                    route_struct_obj.fields.emplace_back(nullptr);  // Explicit PackStream NULL
                }
            } else if (params.impersonated_user.has_value()) {
                writer.set_error(BoltError::SERIALIZATION_ERROR);
                // std::cerr << "Error: impersonated_user provided for ROUTE with Bolt version " << ... << " which does not support it." << std::endl; // Optional log
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

}  // namespace boltprotocol