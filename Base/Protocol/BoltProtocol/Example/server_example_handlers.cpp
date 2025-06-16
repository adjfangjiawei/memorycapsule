#include "server_example_handlers.h"

#include <exception>
#include <optional>  // For HelloMessageParams members
#include <variant>

namespace ServerHandlers {

    // handle_hello_message now also takes the server_negotiated_version
    // to potentially tailor its response or validation based on it.
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

            // Example: If client requested utc patch and server supports it for this version
            if (server_negotiated_version.major == 4 && (server_negotiated_version.minor == 3 || server_negotiated_version.minor == 4)) {
                if (parsed_hello_params.patch_bolt.has_value()) {
                    for (const auto& patch : parsed_hello_params.patch_bolt.value()) {
                        if (patch == "utc") {  // Server agrees to "utc" patch
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
            if (server_resp_ok && !pss_to_write_sptr) {  // Only print if no exception but pss is null
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

    // handle_run_message and deserialize_run_params_from_struct remain the same
    // as they were not directly affected by HelloMessageParams changes.
    // ... (rest of handle_run_message and deserialize_run_params_from_struct) ...
    boltprotocol::BoltError handle_run_message(const boltprotocol::RunMessageParams& run_params, boltprotocol::PackStreamWriter& response_writer) {
        using namespace boltprotocol;
        std::cout << "  Server processing RUN query: '" << run_params.cypher_query << "'" << std::endl;
        auto limit_it = run_params.parameters.find("limit");
        if (limit_it != run_params.parameters.end()) {
            if (const auto* limit_val_ptr = std::get_if<int64_t>(&(limit_it->second))) {
                std::cout << "    With limit: " << *limit_val_ptr << std::endl;
            } else {
                std::cout << "    With limit: (value present but not int64_t)" << std::endl;
            }
        } else {
            std::cout << "    No 'limit' parameter found." << std::endl;
        }

        BoltError err = BoltError::SUCCESS;
        PackStreamStructure pss_obj_on_stack;
        std::shared_ptr<BoltMap> meta_map_sptr;
        std::shared_ptr<BoltList> list_sptr;
        std::shared_ptr<PackStreamStructure> pss_to_write_sptr;

        try {
            SuccessMessageParams run_success_params;
            list_sptr = std::make_shared<BoltList>();
            list_sptr->elements.emplace_back(Value(std::string("name")));
            run_success_params.metadata.emplace("fields", Value(list_sptr));

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
        try {
            SuccessMessageParams summary_success_params;
            summary_success_params.metadata.emplace("type", Value(std::string("r")));

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

    boltprotocol::BoltError deserialize_run_params_from_struct(const boltprotocol::PackStreamStructure& run_struct, boltprotocol::RunMessageParams& out_params) {
        using namespace boltprotocol;
        out_params.cypher_query.clear();
        out_params.parameters.clear();
        out_params.extra_metadata.clear();

        if (run_struct.tag != static_cast<uint8_t>(MessageTag::RUN)) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }
        if (run_struct.fields.size() < 2 || run_struct.fields.size() > 3) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        bool conversion_ok = true;
        try {
            if (std::holds_alternative<std::string>(run_struct.fields[0])) {
                out_params.cypher_query = std::get<std::string>(run_struct.fields[0]);
            } else {
                conversion_ok = false;
            }

            if (conversion_ok && std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct.fields[1])) {
                auto params_map_sptr = std::get<std::shared_ptr<BoltMap>>(run_struct.fields[1]);
                if (params_map_sptr) {
                    out_params.parameters = params_map_sptr->pairs;
                } else {
                    conversion_ok = false;
                }
            } else if (conversion_ok) {
                conversion_ok = false;
            }
            if (conversion_ok && run_struct.fields.size() == 3) {
                if (std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct.fields[2])) {
                    auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(run_struct.fields[2]);
                    if (extra_map_sptr) {
                        out_params.extra_metadata = extra_map_sptr->pairs;
                    }
                } else {
                    conversion_ok = false;
                }
            }
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::bad_variant_access&) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::exception&) {
            return BoltError::UNKNOWN_ERROR;
        }

        return conversion_ok ? BoltError::SUCCESS : BoltError::INVALID_MESSAGE_FORMAT;
    }

}  // namespace ServerHandlers