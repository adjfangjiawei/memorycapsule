#include <exception>  // For std::bad_alloc, std::exception
#include <iomanip>    // For std::setw, std::setfill
#include <iostream>
#include <map>
#include <memory>  // For std::make_shared, std::shared_ptr
#include <string>
#include <variant>  // For std::get_if, std::holds_alternative
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
// #include "boltprotocol/chunking.h"
// #include "boltprotocol/handshake.h"

void print_bolt_error_details_server(const std::string& context, boltprotocol::BoltError err, boltprotocol::PackStreamReader* reader = nullptr, boltprotocol::PackStreamWriter* writer = nullptr) {
    std::cerr << "Error (Server) " << context << ": " << static_cast<int>(err);
    if (reader && reader->has_error() && reader->get_error() != err) {
        std::cerr << " (Reader specific error: " << static_cast<int>(reader->get_error()) << ")";
    }
    if (writer && writer->has_error() && writer->get_error() != err) {
        std::cerr << " (Writer specific error: " << static_cast<int>(writer->get_error()) << ")";
    }
    std::cerr << std::endl;
}

namespace boltprotocol_example_server {

    using namespace ::boltprotocol;

    void print_bytes_server_internal(const std::string& prefix, const std::vector<uint8_t>& bytes) {
        std::cout << prefix;
        for (uint8_t byte : bytes) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        std::cout << std::dec << std::endl;
    }

    BoltError process_run_query(const RunMessageParams& run_params, PackStreamWriter& response_writer) {
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

        // 1. Send SUCCESS for RUN (contains field names)
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

}  // namespace boltprotocol_example_server

int main() {
    using namespace boltprotocol_example_server;
    using namespace boltprotocol;

    std::cout << "Bolt Protocol Server Example (No-Exception Mode)" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    std::vector<uint8_t> server_receive_buffer_storage;
    std::vector<uint8_t> server_send_buffer_storage;
    BoltError err = BoltError::SUCCESS;

    // === Stage 1: Client sends HELLO, Server responds SUCCESS ===
    std::cout << "\nServer expecting HELLO message..." << std::endl;
    server_receive_buffer_storage.clear();
    {
        PackStreamWriter client_hello_writer(server_receive_buffer_storage);
        HelloMessageParams client_hello_params;
        bool client_prep_ok = true;
        try {
            client_hello_params.extra_auth_tokens.emplace("user_agent", Value(std::string("MyExampleCppClient/1.0")));
            client_hello_params.extra_auth_tokens.emplace("scheme", Value(std::string("basic")));
            client_hello_params.extra_auth_tokens.emplace("principal", Value(std::string("neo4j")));
            client_hello_params.extra_auth_tokens.emplace("credentials", Value(std::string("password")));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_server("client sim HELLO (bad_alloc)", BoltError::OUT_OF_MEMORY);
            client_prep_ok = false;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception client sim HELLO: " << e_std.what() << std::endl;
            print_bolt_error_details_server("client sim HELLO (std::exception)", BoltError::UNKNOWN_ERROR);
            client_prep_ok = false;
        }
        if (!client_prep_ok) return 1;

        err = serialize_hello_message(client_hello_params, client_hello_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("client sim serializing HELLO", err, nullptr, &client_hello_writer);
            return 1;
        }
    }
    print_bytes_server_internal("Server received bytes for HELLO (raw): ", server_receive_buffer_storage);

    Value received_value_hello;
    std::shared_ptr<PackStreamStructure> hello_struct_sptr;
    {
        PackStreamReader hello_reader(server_receive_buffer_storage);
        err = hello_reader.read(received_value_hello);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server reading HELLO PSS from received bytes", err, &hello_reader);
            return 1;
        }

        if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(received_value_hello)) {
            print_bolt_error_details_server("Received HELLO message is not a PSS", BoltError::INVALID_MESSAGE_FORMAT, &hello_reader);
            return 1;
        }
        try {
            hello_struct_sptr = std::get<std::shared_ptr<PackStreamStructure>>(std::move(received_value_hello));
        } catch (const std::bad_variant_access&) {
            print_bolt_error_details_server("std::get on HELLO PSS (bad_variant_access)", BoltError::UNKNOWN_ERROR);
            return 1;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception std::get on HELLO PSS: " << e_std.what() << std::endl;
            print_bolt_error_details_server("std::get on HELLO PSS (std::exception)", BoltError::UNKNOWN_ERROR);
            return 1;
        }

        if (!hello_struct_sptr || static_cast<MessageTag>(hello_struct_sptr->tag) != MessageTag::HELLO) {
            print_bolt_error_details_server("Received PSS is not HELLO or sptr is null", BoltError::INVALID_MESSAGE_FORMAT, &hello_reader);
            return 1;
        }
    }
    std::cout << "Server: HELLO message structure received." << std::endl;

    server_send_buffer_storage.clear();
    {
        PackStreamWriter success_hello_writer(server_send_buffer_storage);
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
            print_bolt_error_details_server("server HELLO SUCCESS resp (bad_alloc)", BoltError::OUT_OF_MEMORY);
            server_resp_ok = false;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception server HELLO SUCCESS resp: " << e_std.what() << std::endl;
            print_bolt_error_details_server("server HELLO SUCCESS resp (std::exception)", BoltError::UNKNOWN_ERROR);
            server_resp_ok = false;
        }
        if (!server_resp_ok || !pss_to_write_sptr) {
            if (server_resp_ok) print_bolt_error_details_server("server HELLO SUCCESS resp (null pss_to_write_sptr)", BoltError::OUT_OF_MEMORY);
            return 1;
        }

        err = success_hello_writer.write(Value(std::move(pss_to_write_sptr)));
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server serializing SUCCESS for HELLO", err, nullptr, &success_hello_writer);
            return 1;
        }
    }
    print_bytes_server_internal("Server sending SUCCESS (for HELLO) (raw): ", server_send_buffer_storage);

    // === Stage 2: Client sends RUN, Server processes and responds ===
    std::cout << "\nServer expecting RUN message..." << std::endl;
    server_receive_buffer_storage.clear();
    {
        PackStreamWriter client_run_writer(server_receive_buffer_storage);
        RunMessageParams client_run_params;
        bool client_run_prep_ok = true;
        try {
            client_run_params.cypher_query = "MATCH (n) RETURN n.name AS name LIMIT $limit";
            client_run_params.parameters.emplace("limit", Value(static_cast<int64_t>(5)));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_server("client sim RUN (bad_alloc)", BoltError::OUT_OF_MEMORY);
            client_run_prep_ok = false;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception client sim RUN: " << e_std.what() << std::endl;
            print_bolt_error_details_server("client sim RUN (std::exception)", BoltError::UNKNOWN_ERROR);
            client_run_prep_ok = false;
        }
        if (!client_run_prep_ok) return 1;

        err = serialize_run_message(client_run_params, client_run_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("client sim serializing RUN", err, nullptr, &client_run_writer);
            return 1;
        }
    }
    print_bytes_server_internal("Server received bytes for RUN (raw): ", server_receive_buffer_storage);

    RunMessageParams actual_run_params;
    {
        PackStreamReader run_reader(server_receive_buffer_storage);
        Value received_value_run;
        err = run_reader.read(received_value_run);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server reading RUN PSS from received bytes", err, &run_reader);
            return 1;
        }

        if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(received_value_run)) {
            print_bolt_error_details_server("Received RUN message is not a PSS", BoltError::INVALID_MESSAGE_FORMAT, &run_reader);
            return 1;
        }
        std::shared_ptr<PackStreamStructure> run_struct_sptr;
        try {
            run_struct_sptr = std::get<std::shared_ptr<PackStreamStructure>>(std::move(received_value_run));
        } catch (const std::bad_variant_access&) {
            print_bolt_error_details_server("std::get on RUN PSS (bad_variant_access)", BoltError::UNKNOWN_ERROR);
            return 1;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception std::get on RUN PSS: " << e_std.what() << std::endl;
            print_bolt_error_details_server("std::get on RUN PSS (std::exception)", BoltError::UNKNOWN_ERROR);
            return 1;
        }

        if (!run_struct_sptr || static_cast<MessageTag>(run_struct_sptr->tag) != MessageTag::RUN) {
            print_bolt_error_details_server("Received PSS is not RUN or sptr is null", BoltError::INVALID_MESSAGE_FORMAT, &run_reader);
            return 1;
        }
        std::cout << "Server: RUN message structure received." << std::endl;

        bool run_fields_deser_ok = true;
        try {
            if (run_struct_sptr->fields.size() >= 1 && std::holds_alternative<std::string>(run_struct_sptr->fields[0])) {
                actual_run_params.cypher_query = std::get<std::string>(std::move(run_struct_sptr->fields[0]));
            } else {
                run_fields_deser_ok = false;
            }

            if (run_fields_deser_ok && run_struct_sptr->fields.size() >= 2 && std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct_sptr->fields[1])) {
                auto params_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_sptr->fields[1]));
                if (params_map_sptr) {
                    actual_run_params.parameters = std::move(params_map_sptr->pairs);
                } else {
                    run_fields_deser_ok = false;
                }
            } else if (run_struct_sptr->fields.size() >= 2 && run_fields_deser_ok) {
                // If field 2 exists but is not a map, it's an error.
                // If field 2 doesn't exist, parameters map remains empty, which is valid.
                // This logic means if field[1] is present, it MUST be a map.
                if (run_struct_sptr->fields.size() >= 2) run_fields_deser_ok = false;
            }

            if (run_fields_deser_ok && run_struct_sptr->fields.size() >= 3 && std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct_sptr->fields[2])) {
                auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_sptr->fields[2]));
                if (extra_map_sptr) {
                    actual_run_params.extra_metadata = std::move(extra_map_sptr->pairs);
                }
                // If field 3 exists but is not a map, or if sptr is null, it's fine, extra_metadata just remains empty.
            }
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_server("deserializing RUN fields (bad_alloc)", BoltError::OUT_OF_MEMORY, &run_reader);
            return 1;
        } catch (const std::bad_variant_access&) {
            print_bolt_error_details_server("deserializing RUN fields (bad_variant_access)", BoltError::INVALID_MESSAGE_FORMAT, &run_reader);
            return 1;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception deserializing RUN fields: " << e_std.what() << std::endl;
            print_bolt_error_details_server("deserializing RUN fields (std::exception)", BoltError::UNKNOWN_ERROR, &run_reader);
            return 1;
        }

        if (!run_fields_deser_ok) {
            print_bolt_error_details_server("deserializing RUN fields (logical error)", BoltError::INVALID_MESSAGE_FORMAT, &run_reader);
            return 1;
        }
    }

    server_send_buffer_storage.clear();
    {
        PackStreamWriter run_response_writer(server_send_buffer_storage);
        err = process_run_query(actual_run_params, run_response_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("processing RUN query", err, nullptr, &run_response_writer);
            return 1;
        }
    }
    print_bytes_server_internal("Server sending full response stream for RUN (raw): ", server_send_buffer_storage);

    std::cout << "\nServer example finished." << std::endl;
    return 0;
}