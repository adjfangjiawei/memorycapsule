#include "server_example_handlers.h"

#include <exception>
#include <optional>
#include <variant>

#include "boltprotocol/message_defs.h"  // For versions::Version from bolt_errors_versions.h

namespace ServerHandlers {

    boltprotocol::BoltError handle_hello_message(const boltprotocol::HelloMessageParams& parsed_hello_params, boltprotocol::PackStreamWriter& response_writer, const boltprotocol::versions::Version& server_negotiated_version) {
        using namespace boltprotocol;
        std::cout << "  Server processing HELLO message from: " << parsed_hello_params.user_agent << std::endl;
        if (parsed_hello_params.bolt_agent.has_value()) {
            std::cout << "    Bolt Agent Product: " << parsed_hello_params.bolt_agent.value().product << std::endl;
        }
        if (parsed_hello_params.auth_scheme.has_value()) {
            std::cout << "    Auth Scheme: " << parsed_hello_params.auth_scheme.value() << std::endl;
        }

        SuccessMessageParams success_for_hello_params;
        PackStreamStructure pss_obj_on_stack;
        std::shared_ptr<BoltMap> meta_map_sptr;
        std::shared_ptr<PackStreamStructure> pss_to_write_sptr;
        bool server_resp_ok = true;

        try {
            success_for_hello_params.metadata.emplace("connection_id", Value(std::string("server-conn-xyz")));
            success_for_hello_params.metadata.emplace("server", Value(std::string("MyExampleBoltServer/0.1 (Bolt ") + std::to_string(server_negotiated_version.major) + "." + std::to_string(server_negotiated_version.minor) + ")"));

            if (server_negotiated_version.major == 4 && (server_negotiated_version.minor == 3 || server_negotiated_version.minor == 4)) {
                if (parsed_hello_params.patch_bolt.has_value()) {
                    for (const auto& patch : parsed_hello_params.patch_bolt.value()) {
                        if (patch == "utc") {
                            auto agreed_patches_list = std::make_shared<BoltList>();
                            agreed_patches_list->elements.emplace_back(Value(std::string("utc")));
                            success_for_hello_params.metadata.emplace("patch_bolt", Value(agreed_patches_list));
                            std::cout << "    Server agreed to 'utc' patch." << std::endl;
                            break;
                        }
                    }
                }
            }

            pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            meta_map_sptr = std::make_shared<BoltMap>();
            meta_map_sptr->pairs = std::move(success_for_hello_params.metadata);
            pss_obj_on_stack.fields.emplace_back(Value(meta_map_sptr));
            pss_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_obj_on_stack));

        } catch (const std::bad_alloc&) {
            print_bolt_error_details_server("server HELLO SUCCESS resp (bad_alloc)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
            response_writer.set_error(BoltError::OUT_OF_MEMORY);
            server_resp_ok = false;
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception server HELLO SUCCESS resp: " << e_std.what() << std::endl;
            print_bolt_error_details_server("server HELLO SUCCESS resp (std::exception)", BoltError::UNKNOWN_ERROR, nullptr, &response_writer);
            response_writer.set_error(BoltError::UNKNOWN_ERROR);
            server_resp_ok = false;
            return BoltError::UNKNOWN_ERROR;
        }

        if (!server_resp_ok || !pss_to_write_sptr) {
            if (server_resp_ok && !pss_to_write_sptr) {
                print_bolt_error_details_server("server HELLO SUCCESS resp (null pss_to_write_sptr)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
                response_writer.set_error(BoltError::OUT_OF_MEMORY);
            }
            return response_writer.get_error();
        }

        BoltError err = response_writer.write(Value(std::move(pss_to_write_sptr)));
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server serializing SUCCESS for HELLO", err, nullptr, &response_writer);
        }
        return err;
    }

    // handle_run_message now receives fully parsed RunMessageParams.
    // It no longer needs to deserialize from a raw PackStreamStructure itself.
    boltprotocol::BoltError handle_run_message(const boltprotocol::RunMessageParams& run_params,  // Already parsed
                                               boltprotocol::PackStreamWriter& response_writer) {
        using namespace boltprotocol;
        std::cout << "  Server processing RUN query: '" << run_params.cypher_query << "'" << std::endl;

        // Access typed optional fields
        if (run_params.db.has_value()) {
            std::cout << "    For database: " << run_params.db.value() << std::endl;
        }
        if (run_params.tx_timeout.has_value()) {
            std::cout << "    With tx_timeout: " << run_params.tx_timeout.value() << "ms" << std::endl;
        }

        // Access cypher parameters
        auto limit_it = run_params.parameters.find("limit");
        if (limit_it != run_params.parameters.end()) {
            if (const auto* limit_val_ptr = std::get_if<int64_t>(&(limit_it->second))) {
                std::cout << "    With limit parameter: " << *limit_val_ptr << std::endl;
            }
        }
        // Access other_extra_fields if needed
        if (!run_params.other_extra_fields.empty()) {
            std::cout << "    With other extra fields:" << std::endl;
            for (const auto& pair : run_params.other_extra_fields) {
                std::cout << "      " << pair.first << ": (type " << pair.second.index() << ")" << std::endl;
            }
        }

        BoltError err = BoltError::SUCCESS;
        PackStreamStructure pss_obj_on_stack;
        std::shared_ptr<BoltMap> meta_map_sptr;
        std::shared_ptr<BoltList> list_sptr;
        std::shared_ptr<PackStreamStructure> pss_to_write_sptr;

        // 1. Send SUCCESS for RUN (contains field names)
        try {
            SuccessMessageParams run_success_params;
            list_sptr = std::make_shared<BoltList>();
            list_sptr->elements.emplace_back(Value(std::string("name")));
            run_success_params.metadata.emplace("fields", Value(list_sptr));
            // Optionally add qid for explicit transactions, or t_first for auto-commit
            // run_success_params.metadata.emplace("t_first", Value(static_cast<int64_t>(10))); // Example

            pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            pss_obj_on_stack.fields.clear();

            meta_map_sptr = std::make_shared<BoltMap>();
            meta_map_sptr->pairs = std::move(run_success_params.metadata);
            pss_obj_on_stack.fields.emplace_back(Value(meta_map_sptr));

            pss_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_obj_on_stack));

        } catch (const std::bad_alloc&) {
            print_bolt_error_details_server("preparing RUN SUCCESS (bad_alloc)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
            response_writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception server preparing RUN SUCCESS: " << e_std.what() << std::endl;
            print_bolt_error_details_server("preparing RUN SUCCESS (std::exception)", BoltError::UNKNOWN_ERROR, nullptr, &response_writer);
            response_writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_to_write_sptr) {
            print_bolt_error_details_server("preparing RUN SUCCESS (null pss_to_write_sptr)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
            response_writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        err = response_writer.write(Value(std::move(pss_to_write_sptr)));
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("serializing SUCCESS for RUN", err, nullptr, &response_writer);
            return err;
        }
        std::cout << "  Server sent SUCCESS for RUN (with fields)." << std::endl;

        // 2. Send RECORD messages (dummy data)
        for (int i = 0; i < 2; ++i) {
            try {
                RecordMessageParams record_params;
                record_params.fields.emplace_back(Value(std::string("Node " + std::to_string(i))));

                pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::RECORD);
                pss_obj_on_stack.fields.clear();
                list_sptr = std::make_shared<BoltList>();
                list_sptr->elements = std::move(record_params.fields);
                pss_obj_on_stack.fields.emplace_back(Value(list_sptr));

                pss_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_obj_on_stack));

            } catch (const std::bad_alloc&) {
                print_bolt_error_details_server("preparing RECORD " + std::to_string(i) + " (bad_alloc)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
                response_writer.set_error(BoltError::OUT_OF_MEMORY);
                return BoltError::OUT_OF_MEMORY;
            } catch (const std::exception& e_std) {
                std::cerr << "Std exception server preparing RECORD " << std::to_string(i) << ": " << e_std.what() << std::endl;
                print_bolt_error_details_server("preparing RECORD " + std::to_string(i) + " (std::exception)", BoltError::UNKNOWN_ERROR, nullptr, &response_writer);
                response_writer.set_error(BoltError::UNKNOWN_ERROR);
                return BoltError::UNKNOWN_ERROR;
            }

            if (!pss_to_write_sptr) {
                print_bolt_error_details_server("preparing RECORD (null pss_to_write_sptr)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
                response_writer.set_error(BoltError::OUT_OF_MEMORY);
                return BoltError::OUT_OF_MEMORY;
            }
            err = response_writer.write(Value(std::move(pss_to_write_sptr)));
            if (err != BoltError::SUCCESS) {
                print_bolt_error_details_server("serializing RECORD " + std::to_string(i), err, nullptr, &response_writer);
                return err;
            }
            std::cout << "  Server sent RECORD " << i << "." << std::endl;
        }
        // 3. Send final SUCCESS (summary)
        try {
            SuccessMessageParams summary_success_params;
            summary_success_params.metadata.emplace("type", Value(std::string("r")));
            // For auto-commit that's now finished:
            // summary_success_params.metadata.emplace("bookmark", Value(std::string("neo4j:bookmark:v1:tx42")));
            // summary_success_params.metadata.emplace("has_more", Value(false)); // If Bolt 4.0+

            pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            pss_obj_on_stack.fields.clear();
            meta_map_sptr = std::make_shared<BoltMap>();
            meta_map_sptr->pairs = std::move(summary_success_params.metadata);
            pss_obj_on_stack.fields.emplace_back(Value(meta_map_sptr));

            pss_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_obj_on_stack));

        } catch (const std::bad_alloc&) {
            print_bolt_error_details_server("preparing summary SUCCESS (bad_alloc)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
            response_writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception server preparing summary SUCCESS: " << e_std.what() << std::endl;
            print_bolt_error_details_server("preparing summary SUCCESS (std::exception)", BoltError::UNKNOWN_ERROR, nullptr, &response_writer);
            response_writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_to_write_sptr) {
            print_bolt_error_details_server("preparing summary SUCCESS (null pss_to_write_sptr)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
            response_writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        err = response_writer.write(Value(std::move(pss_to_write_sptr)));
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("serializing SUCCESS summary", err, nullptr, &response_writer);
            return err;
        }
        std::cout << "  Server sent SUCCESS summary." << std::endl;
        return BoltError::SUCCESS;
    }

    // This helper is no longer strictly needed if server_example_main.cpp directly uses
    // deserialize_run_message_request. If it were kept, it would need significant rework
    // to populate the new RunMessageParams structure correctly from a raw PackStreamStructure.
    // For now, let's comment it out as its functionality is superseded.
    /*
    boltprotocol::BoltError deserialize_run_params_from_struct(
        const boltprotocol::PackStreamStructure& run_struct,
        boltprotocol::RunMessageParams& out_params) {
        // ... This would need to parse run_struct.fields and populate the new
        //     std::optional members of out_params and other_extra_fields ...
        print_bolt_error_details_server("deserialize_run_params_from_struct is deprecated", boltprotocol::BoltError::UNKNOWN_ERROR);
        return boltprotocol::BoltError::UNKNOWN_ERROR; // Placeholder
    }
    */

}  // namespace ServerHandlers