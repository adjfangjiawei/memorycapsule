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
// #include "boltprotocol/chunking.h" // Uncomment if chunking is integrated for this example
// #include "boltprotocol/handshake.h" // Uncomment if handshake is integrated for this example

// Helper to print BoltError and associated reader/writer errors (consistent with client_example)
void print_bolt_error_details_server(const std::string& context, boltprotocol::BoltError err, boltprotocol::PackStreamReader* reader = nullptr, boltprotocol::PackStreamWriter* writer = nullptr) {
    std::cerr << "Error (Server) " << context << ": " << static_cast<int>(err);
    if (reader && reader->has_error() && reader->get_error() != err) {  // Print only if different from main error
        std::cerr << " (Reader specific error: " << static_cast<int>(reader->get_error()) << ")";
    }
    if (writer && writer->has_error() && writer->get_error() != err) {  // Print only if different from main error
        std::cerr << " (Writer specific error: " << static_cast<int>(writer->get_error()) << ")";
    }
    std::cerr << std::endl;
}

// Using a namespace for the example server code
namespace boltprotocol_example_server {

    using namespace ::boltprotocol;  // Make library types available

    // Helper function to print a byte vector (for debugging within this namespace)
    void print_bytes_server_internal(const std::string& prefix, const std::vector<uint8_t>& bytes) {
        std::cout << prefix;
        for (uint8_t byte : bytes) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        std::cout << std::dec << std::endl;
    }

    // Simulates processing a RUN query and preparing responses
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
        PackStreamStructure pss_obj_on_stack;  // Reusable stack object for PSS construction
        std::shared_ptr<BoltMap> meta_map_sptr;
        std::shared_ptr<BoltList> list_sptr;
        std::shared_ptr<PackStreamStructure> pss_to_write_sptr;

        // 1. Send SUCCESS for RUN (contains field names)
        try {
            SuccessMessageParams run_success_params;  // Params struct on stack
            list_sptr = std::make_shared<BoltList>();
            list_sptr->elements.emplace_back(Value(std::string("name")));     // emplace_back can throw (bad_alloc)
            run_success_params.metadata.emplace("fields", Value(list_sptr));  // map::emplace can throw (bad_alloc)

            pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            pss_obj_on_stack.fields.clear();  // Reuse fields vector

            meta_map_sptr = std::make_shared<BoltMap>();
            meta_map_sptr->pairs = std::move(run_success_params.metadata);  // Move pairs into map
            pss_obj_on_stack.fields.emplace_back(Value(meta_map_sptr));     // Emplace Value (which copies shared_ptr)

            pss_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_obj_on_stack));  // Move pss_obj into new PSS

        } catch (const std::bad_alloc&) {
            print_bolt_error_details_server("preparing RUN SUCCESS (bad_alloc)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
            response_writer.set_error(BoltError::OUT_OF_MEMORY);  // Inform writer about the error origin
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception server preparing RUN SUCCESS: " << e_std.what() << std::endl;
            print_bolt_error_details_server("preparing RUN SUCCESS (std::exception)", BoltError::UNKNOWN_ERROR, nullptr, &response_writer);
            response_writer.set_error(BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        if (!pss_to_write_sptr) {  // Should be redundant if make_shared throws or we catch bad_alloc
            print_bolt_error_details_server("preparing RUN SUCCESS (null pss_to_write_sptr)", BoltError::OUT_OF_MEMORY, nullptr, &response_writer);
            response_writer.set_error(BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }
        err = response_writer.write(Value(std::move(pss_to_write_sptr)));  // Move sptr into Value
        if (err != BoltError::SUCCESS) {
            // response_writer.write would have set its internal error.
            print_bolt_error_details_server("serializing SUCCESS for RUN", err, nullptr, &response_writer);
            return err;  // Propagate the error from writer
        }
        std::cout << "  Server sent SUCCESS for RUN (with fields)." << std::endl;

        // 2. Send RECORD messages (dummy data)
        for (int i = 0; i < 2; ++i) {
            try {
                RecordMessageParams record_params;  // Params struct on stack
                record_params.fields.emplace_back(Value(std::string("Node " + std::to_string(i))));

                pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::RECORD);
                pss_obj_on_stack.fields.clear();                         // Reuse
                list_sptr = std::make_shared<BoltList>();                // New list for this record
                list_sptr->elements = std::move(record_params.fields);   // Move elements
                pss_obj_on_stack.fields.emplace_back(Value(list_sptr));  // Value copies sptr

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
            SuccessMessageParams summary_success_params;  // Params on stack
            summary_success_params.metadata.emplace("type", Value(std::string("r")));

            pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            pss_obj_on_stack.fields.clear();              // Reuse
            meta_map_sptr = std::make_shared<BoltMap>();  // New map for summary
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
    using namespace boltprotocol_example_server;  // For process_run_query, print_bytes_server_internal
    using namespace boltprotocol;                 // For BoltError, MessageTag, PackStreamReader/Writer, message structs etc.

    std::cout << "Bolt Protocol Server Example (No-Exception Mode)" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    std::vector<uint8_t> server_receive_buffer_storage;  // For data received from client
    std::vector<uint8_t> server_send_buffer_storage;     // For data to send to client
    BoltError err = BoltError::SUCCESS;

    // === Stage 1: Client sends HELLO, Server responds SUCCESS ===
    std::cout << "\nServer expecting HELLO message..." << std::endl;
    server_receive_buffer_storage.clear();
    {  // Simulate client sending HELLO
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
            // serialize_hello_message would have set writer's error state.
            print_bolt_error_details_server("client sim serializing HELLO", err, nullptr, &client_hello_writer);
            return 1;
        }
    }  // client_hello_writer out of scope
    print_bytes_server_internal("Server received bytes for HELLO (raw): ", server_receive_buffer_storage);

    // Server deserializes HELLO
    Value received_value_hello;
    std::shared_ptr<PackStreamStructure> hello_struct_sptr;  // To store the deserialized HELLO PSS
    {                                                        // Scope for hello_reader
        PackStreamReader hello_reader(server_receive_buffer_storage);
        err = hello_reader.read(received_value_hello);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server reading HELLO PSS from received bytes", err, &hello_reader);
            return 1;
        }

        // Validate and extract PackStreamStructure from Value
        if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(received_value_hello)) {
            print_bolt_error_details_server("Received HELLO message is not a PSS", BoltError::INVALID_MESSAGE_FORMAT, &hello_reader);
            // hello_reader.set_error(BoltError::INVALID_MESSAGE_FORMAT); // Reader might not know it's a message format error yet
            return 1;
        }
        try {  // std::get on variant can throw bad_variant_access if holds_alternative was false (defensive)
            hello_struct_sptr = std::get<std::shared_ptr<PackStreamStructure>>(std::move(received_value_hello));
        } catch (const std::bad_variant_access&) {  // Should not happen given holds_alternative
            print_bolt_error_details_server("std::get on HELLO PSS (bad_variant_access)", BoltError::UNKNOWN_ERROR);
            return 1;
        } catch (const std::exception& e_std) {  // Other issues with std::get or Value move
            std::cerr << "Std exception std::get on HELLO PSS: " << e_std.what() << std::endl;
            print_bolt_error_details_server("std::get on HELLO PSS (std::exception)", BoltError::UNKNOWN_ERROR);
            return 1;
        }

        if (!hello_struct_sptr || static_cast<MessageTag>(hello_struct_sptr->tag) != MessageTag::HELLO) {
            print_bolt_error_details_server("Received PSS is not HELLO or sptr is null", BoltError::INVALID_MESSAGE_FORMAT, &hello_reader);
            return 1;
        }
    }  // hello_reader out of scope
    std::cout << "Server: HELLO message structure received." << std::endl;
    // Client would typically inspect hello_struct_sptr->fields[0] (the metadata map) here.
    // For example:
    // if (!hello_struct_sptr->fields.empty() && std::holds_alternative<std::shared_ptr<BoltMap>>(hello_struct_sptr->fields[0])) {
    //     auto metadata_map = std::get<std::shared_ptr<BoltMap>>(hello_struct_sptr->fields[0]);
    //     if(metadata_map) { /* access metadata_map->pairs */ }
    // }

    // Server sends SUCCESS in response to HELLO
    server_send_buffer_storage.clear();
    {  // Scope for success_hello_writer
        PackStreamWriter success_hello_writer(server_send_buffer_storage);
        SuccessMessageParams success_for_hello_params;  // Params on stack
        PackStreamStructure pss_obj_on_stack;           // PSS on stack
        std::shared_ptr<BoltMap> meta_map_sptr;
        std::shared_ptr<PackStreamStructure> pss_to_write_sptr;
        bool server_resp_ok = true;

        try {
            success_for_hello_params.metadata.emplace("connection_id", Value(std::string("server-conn-xyz")));
            success_for_hello_params.metadata.emplace("server", Value(std::string("MyExampleBoltServer/0.1")));

            pss_obj_on_stack.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            meta_map_sptr = std::make_shared<BoltMap>();
            meta_map_sptr->pairs = std::move(success_for_hello_params.metadata);
            pss_obj_on_stack.fields.emplace_back(Value(meta_map_sptr));  // Value copies sptr

            pss_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_obj_on_stack));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_server("server HELLO SUCCESS resp (bad_alloc)", BoltError::OUT_OF_MEMORY);
            server_resp_ok = false;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception server HELLO SUCCESS resp: " << e_std.what() << std::endl;
            print_bolt_error_details_server("server HELLO SUCCESS resp (std::exception)", BoltError::UNKNOWN_ERROR);
            server_resp_ok = false;
        }
        if (!server_resp_ok || !pss_to_write_sptr) {  // Check if allocation failed or sptr is null
            if (server_resp_ok) print_bolt_error_details_server("server HELLO SUCCESS resp (null pss_to_write_sptr)", BoltError::OUT_OF_MEMORY);
            return 1;
        }

        err = success_hello_writer.write(Value(std::move(pss_to_write_sptr)));
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server serializing SUCCESS for HELLO", err, nullptr, &success_hello_writer);
            return 1;
        }
    }  // success_hello_writer out of scope
    print_bytes_server_internal("Server sending SUCCESS (for HELLO) (raw): ", server_send_buffer_storage);

    // === Stage 2: Client sends RUN, Server processes and responds ===
    std::cout << "\nServer expecting RUN message..." << std::endl;
    server_receive_buffer_storage.clear();
    {  // Simulate client sending RUN
        PackStreamWriter client_run_writer(server_receive_buffer_storage);
        RunMessageParams client_run_params;  // Params on stack
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
    }  // client_run_writer out of scope
    print_bytes_server_internal("Server received bytes for RUN (raw): ", server_receive_buffer_storage);

    // Server deserializes RUN
    RunMessageParams actual_run_params;  // To store deserialized RUN params
    {                                    // Scope for run_reader
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
        std::shared_ptr<PackStreamStructure> run_struct_sptr;  // To hold the PSS from Value
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

        // Deserialize RUN PSS fields into actual_run_params
        bool run_fields_deser_ok = true;
        try {  // try-catch for std::get and potential moves from Value
            if (run_struct_sptr->fields.size() >= 1 && std::holds_alternative<std::string>(run_struct_sptr->fields[0])) {
                actual_run_params.cypher_query = std::get<std::string>(std::move(run_struct_sptr->fields[0]));
            } else {
                run_fields_deser_ok = false;
            }  // Query is mandatory

            if (run_fields_deser_ok && run_struct_sptr->fields.size() >= 2 && std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct_sptr->fields[1])) {
                auto params_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_sptr->fields[1]));
                if (params_map_sptr) {
                    actual_run_params.parameters = std::move(params_map_sptr->pairs);
                } else {
                    run_fields_deser_ok = false;
                }  // Params map itself should not be null shared_ptr if present
            } else if (run_struct_sptr->fields.size() >= 2) {  // Parameters field missing or wrong type
                run_fields_deser_ok = false;
            }  // If less than 2 fields, parameters are considered empty, which is valid for RUN.

            // Extra metadata is optional (field 3)
            if (run_fields_deser_ok && run_struct_sptr->fields.size() >= 3 && std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct_sptr->fields[2])) {
                auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_sptr->fields[2]));
                if (extra_map_sptr) {  // If a map is provided (even if empty), use it. Null sptr is fine if field missing.
                    actual_run_params.extra_metadata = std::move(extra_map_sptr->pairs);
                }
            }
        } catch (const std::bad_alloc&) {  // If std::string move or std::map move allocates
            print_bolt_error_details_server("deserializing RUN fields (bad_alloc)", BoltError::OUT_OF_MEMORY, &run_reader);
            return 1;
        } catch (const std::bad_variant_access&) {  // Defensive, should be caught by holds_alternative
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
    }  // run_reader out of scope

    // Server processes RUN and sends response stream
    server_send_buffer_storage.clear();
    {  // Scope for run_response_writer
        PackStreamWriter run_response_writer(server_send_buffer_storage);
        err = process_run_query(actual_run_params, run_response_writer);
        if (err != BoltError::SUCCESS) {
            // process_run_query should have printed its specific error.
            // It also sets the response_writer's error state if it originated the error.
            print_bolt_error_details_server("processing RUN query", err, nullptr, &run_response_writer);
            return 1;
        }
    }  // run_response_writer out of scope
    print_bytes_server_internal("Server sending full response stream for RUN (raw): ", server_send_buffer_storage);

    std::cout << "\nServer example finished." << std::endl;
    return 0;
}