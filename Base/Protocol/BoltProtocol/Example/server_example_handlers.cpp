#include "server_example_handlers.h"

#include <exception>  // For std::bad_alloc, std::exception
#include <variant>    // For std::get_if, std::holds_alternative

namespace ServerHandlers {

    // Simplified handle_hello_message. A real server would inspect hello_params.extra_auth_tokens for authentication.
    boltprotocol::BoltError handle_hello_message(const boltprotocol::HelloMessageParams& hello_params, boltprotocol::PackStreamWriter& response_writer) {
        using namespace boltprotocol;
        std::cout << "  Server processing HELLO message." << std::endl;
        // Example: Log received user agent
        auto ua_it = hello_params.extra_auth_tokens.find("user_agent");
        if (ua_it != hello_params.extra_auth_tokens.end()) {
            if (const auto* ua_str = std::get_if<std::string>(&(ua_it->second))) {
                std::cout << "    User-Agent: " << *ua_str << std::endl;
            }
        }

        SuccessMessageParams success_for_hello_params;
        PackStreamStructure pss_obj_on_stack;
        std::shared_ptr<BoltMap> meta_map_sptr;
        std::shared_ptr<PackStreamStructure> pss_to_write_sptr;
        bool server_resp_ok = true;

        try {
            success_for_hello_params.metadata.emplace("connection_id", Value(std::string("server-conn-xyz")));
            success_for_hello_params.metadata.emplace("server", Value(std::string("MyExampleBoltServer/0.1")));

            pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            meta_map_sptr = std::make_shared<BoltMap>();
            meta_map_sptr->pairs = std::move(success_for_hello_params.metadata);
            pss_obj_on_stack.fields.emplace_back(Value(meta_map_sptr));
            pss_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_obj_on_stack));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_server("server HELLO SUCCESS resp (bad_alloc)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
            response_writer.set_error(BoltError::OUT_OF_MEMORY);  // Ensure writer knows
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
            if (server_resp_ok) print_bolt_error_details_server("server HELLO SUCCESS resp (null pss_to_write_sptr)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
            if (server_resp_ok) response_writer.set_error(BoltError::OUT_OF_MEMORY);  // if pss_to_write_sptr is null but no exception
            return response_writer.get_error();                                       // Return the error state of the writer
        }

        BoltError err = response_writer.write(Value(std::move(pss_to_write_sptr)));
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server serializing SUCCESS for HELLO", err, nullptr, &response_writer);
            // err is already set in response_writer by the write call.
        }
        return err;
    }

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
        PackStreamStructure pss_obj_on_stack;  // Reused for different PSS constructions
        std::shared_ptr<BoltMap> meta_map_sptr;
        std::shared_ptr<BoltList> list_sptr;
        std::shared_ptr<PackStreamStructure> pss_to_write_sptr;

        // 1. Send SUCCESS for RUN (contains field names)
        try {
            SuccessMessageParams run_success_params;
            list_sptr = std::make_shared<BoltList>();
            list_sptr->elements.emplace_back(Value(std::string("name")));  // Dummy field name
            run_success_params.metadata.emplace("fields", Value(list_sptr));
            // Optionally add qid if server supports it: run_success_params.metadata.emplace("qid", Value(static_cast<int64_t>(123)));

            pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            pss_obj_on_stack.fields.clear();  // Clear for reuse

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

        if (!pss_to_write_sptr) {  // Should be caught by bad_alloc if make_shared fails typically
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
        for (int i = 0; i < 2; ++i) {  // Send two dummy records
            try {
                RecordMessageParams record_params;
                record_params.fields.emplace_back(Value(std::string("Node " + std::to_string(i))));

                pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::RECORD);
                pss_obj_on_stack.fields.clear();
                list_sptr = std::make_shared<BoltList>();  // New list for this record
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
            summary_success_params.metadata.emplace("type", Value(std::string("r")));  // Query type 'r' for read

            pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            pss_obj_on_stack.fields.clear();
            meta_map_sptr = std::make_shared<BoltMap>();  // New map for this summary
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
            return BoltError::INVALID_MESSAGE_FORMAT;  // Should be checked by caller too
        }

        // RUN <query_string> <params_map> <extra_map>
        if (run_struct.fields.size() < 2 || run_struct.fields.size() > 3) {
            return BoltError::INVALID_MESSAGE_FORMAT;
        }

        bool conversion_ok = true;
        try {
            // Field 0: Cypher query (string)
            if (std::holds_alternative<std::string>(run_struct.fields[0])) {
                out_params.cypher_query = std::get<std::string>(run_struct.fields[0]);
            } else {
                conversion_ok = false;
            }

            // Field 1: Parameters map
            if (conversion_ok && std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct.fields[1])) {
                auto params_map_sptr = std::get<std::shared_ptr<BoltMap>>(run_struct.fields[1]);
                if (params_map_sptr) {
                    out_params.parameters = params_map_sptr->pairs;  // This is a copy
                } else {
                    conversion_ok = false;  // Null map pointer
                }
            } else if (conversion_ok) {  // Field 1 not a map
                conversion_ok = false;
            }

            // Field 2: Extra metadata map (optional)
            if (conversion_ok && run_struct.fields.size() == 3) {
                if (std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct.fields[2])) {
                    auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(run_struct.fields[2]);
                    if (extra_map_sptr) {
                        out_params.extra_metadata = extra_map_sptr->pairs;  // Copy
                    }
                    // If it's a null map shared_ptr, extra_metadata remains empty, which is fine.
                } else {  // Field 2 present but not a map
                    conversion_ok = false;
                }
            }
        } catch (const std::bad_alloc&) {
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::bad_variant_access&) {  // Should be caught by holds_alternative
            return BoltError::INVALID_MESSAGE_FORMAT;
        } catch (const std::exception&) {
            return BoltError::UNKNOWN_ERROR;  // Other potential exceptions from string/map copy
        }

        return conversion_ok ? BoltError::SUCCESS : BoltError::INVALID_MESSAGE_FORMAT;
    }

}  // namespace ServerHandlers