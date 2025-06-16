#include <iostream>
#include <map>
#include <memory>  // For std::shared_ptr, std::make_shared
#include <string>
#include <variant>  // For std::holds_alternative, std::get
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"  // For client-side serialization to simulate requests
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "server_example_handlers.h"
#include "server_example_utils.h"

// Helper to simulate client sending a HELLO message
// Returns raw bytes for the server to process
boltprotocol::BoltError simulate_client_hello(std::vector<uint8_t>& out_raw_bytes) {
    using namespace boltprotocol;
    out_raw_bytes.clear();
    PackStreamWriter client_hello_writer(out_raw_bytes);
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
        return BoltError::OUT_OF_MEMORY;
    } catch (const std::exception& e_std) {
        std::cerr << "Std exception client sim HELLO: " << e_std.what() << std::endl;
        print_bolt_error_details_server("client sim HELLO (std::exception)", BoltError::UNKNOWN_ERROR);
        client_prep_ok = false;
        return BoltError::UNKNOWN_ERROR;
    }
    if (!client_prep_ok) return BoltError::UNKNOWN_ERROR;  // Should be more specific

    BoltError err = serialize_hello_message(client_hello_params, client_hello_writer);
    if (err != BoltError::SUCCESS) {
        print_bolt_error_details_server("client sim serializing HELLO", err, nullptr, &client_hello_writer);
    }
    return err;
}

// Helper to simulate client sending a RUN message
boltprotocol::BoltError simulate_client_run(std::vector<uint8_t>& out_raw_bytes) {
    using namespace boltprotocol;
    out_raw_bytes.clear();
    PackStreamWriter client_run_writer(out_raw_bytes);
    RunMessageParams client_run_params;
    bool client_run_prep_ok = true;
    try {
        client_run_params.cypher_query = "MATCH (n) RETURN n.name AS name LIMIT $limit";
        client_run_params.parameters.emplace("limit", Value(static_cast<int64_t>(5)));
    } catch (const std::bad_alloc&) {
        print_bolt_error_details_server("client sim RUN (bad_alloc)", BoltError::OUT_OF_MEMORY);
        client_run_prep_ok = false;
        return BoltError::OUT_OF_MEMORY;
    } catch (const std::exception& e_std) {
        std::cerr << "Std exception client sim RUN: " << e_std.what() << std::endl;
        print_bolt_error_details_server("client sim RUN (std::exception)", BoltError::UNKNOWN_ERROR);
        client_run_prep_ok = false;
        return BoltError::UNKNOWN_ERROR;
    }
    if (!client_run_prep_ok) return BoltError::UNKNOWN_ERROR;

    BoltError err = serialize_run_message(client_run_params, client_run_writer);
    if (err != BoltError::SUCCESS) {
        print_bolt_error_details_server("client sim serializing RUN", err, nullptr, &client_run_writer);
    }
    return err;
}

int main() {
    using namespace boltprotocol;

    std::cout << "Bolt Protocol Server Example (Refactored, No-Exception Mode)" << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;

    std::vector<uint8_t> server_receive_buffer_storage;
    std::vector<uint8_t> server_send_buffer_storage;
    BoltError err = BoltError::SUCCESS;

    // === Stage 1: Client sends HELLO, Server responds SUCCESS ===
    std::cout << "\nServer expecting HELLO message..." << std::endl;
    err = simulate_client_hello(server_receive_buffer_storage);
    if (err != BoltError::SUCCESS) return 1;
    print_bytes_server("Server received bytes for HELLO (raw): ", server_receive_buffer_storage);

    Value received_value_hello_wrapper;
    std::shared_ptr<PackStreamStructure> hello_struct_sptr;
    {
        PackStreamReader hello_reader(server_receive_buffer_storage);
        err = hello_reader.read(received_value_hello_wrapper);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server reading HELLO PSS from received bytes", err, &hello_reader);
            return 1;
        }

        if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(received_value_hello_wrapper)) {
            print_bolt_error_details_server("Received HELLO message is not a PSS", BoltError::INVALID_MESSAGE_FORMAT, &hello_reader);
            return 1;
        }
        try {
            hello_struct_sptr = std::get<std::shared_ptr<PackStreamStructure>>(std::move(received_value_hello_wrapper));
        } catch (const std::exception&) { /* Should be caught by holds_alternative or be bad_alloc */
            print_bolt_error_details_server("std::get on HELLO PSS", BoltError::UNKNOWN_ERROR);
            return 1;
        }

        if (!hello_struct_sptr || static_cast<MessageTag>(hello_struct_sptr->tag) != MessageTag::HELLO) {
            print_bolt_error_details_server("Received PSS is not HELLO or sptr is null", BoltError::INVALID_MESSAGE_FORMAT, &hello_reader);
            return 1;
        }
    }
    std::cout << "Server: HELLO message structure received." << std::endl;

    // Extract HelloMessageParams (simplified for this example)
    HelloMessageParams actual_hello_params;
    if (hello_struct_sptr && hello_struct_sptr->fields.size() == 1 && std::holds_alternative<std::shared_ptr<BoltMap>>(hello_struct_sptr->fields[0])) {
        auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(hello_struct_sptr->fields[0]);
        if (extra_map_sptr) {
            try {
                actual_hello_params.extra_auth_tokens = extra_map_sptr->pairs;
            }  // Copy
            catch (const std::bad_alloc&) {
                print_bolt_error_details_server("Copying HELLO params map", BoltError::OUT_OF_MEMORY);
                return 1;
            } catch (...) {
                print_bolt_error_details_server("Copying HELLO params map", BoltError::UNKNOWN_ERROR);
                return 1;
            }
        }
    } else {
        print_bolt_error_details_server("Parsing HELLO PSS fields for extra_auth_tokens", BoltError::INVALID_MESSAGE_FORMAT);
        return 1;
    }

    server_send_buffer_storage.clear();
    {
        PackStreamWriter success_hello_writer(server_send_buffer_storage);
        err = ServerHandlers::handle_hello_message(actual_hello_params, success_hello_writer);
        if (err != BoltError::SUCCESS) {
            // Error already printed by handler or writer
            return 1;
        }
    }
    print_bytes_server("Server sending SUCCESS (for HELLO) (raw): ", server_send_buffer_storage);

    // === Stage 2: Client sends RUN, Server processes and responds ===
    std::cout << "\nServer expecting RUN message..." << std::endl;
    err = simulate_client_run(server_receive_buffer_storage);
    if (err != BoltError::SUCCESS) return 1;
    print_bytes_server("Server received bytes for RUN (raw): ", server_receive_buffer_storage);

    Value received_value_run_wrapper;
    std::shared_ptr<PackStreamStructure> run_struct_sptr;
    {
        PackStreamReader run_reader(server_receive_buffer_storage);
        err = run_reader.read(received_value_run_wrapper);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server reading RUN PSS from received bytes", err, &run_reader);
            return 1;
        }

        if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(received_value_run_wrapper)) {
            print_bolt_error_details_server("Received RUN message is not a PSS", BoltError::INVALID_MESSAGE_FORMAT, &run_reader);
            return 1;
        }
        try {
            run_struct_sptr = std::get<std::shared_ptr<PackStreamStructure>>(std::move(received_value_run_wrapper));
        } catch (const std::exception&) {
            print_bolt_error_details_server("std::get on RUN PSS", BoltError::UNKNOWN_ERROR);
            return 1;
        }

        if (!run_struct_sptr || static_cast<MessageTag>(run_struct_sptr->tag) != MessageTag::RUN) {
            print_bolt_error_details_server("Received PSS is not RUN or sptr is null", BoltError::INVALID_MESSAGE_FORMAT, &run_reader);
            return 1;
        }
    }
    std::cout << "Server: RUN message structure received." << std::endl;

    RunMessageParams actual_run_params;
    err = ServerHandlers::deserialize_run_params_from_struct(*run_struct_sptr, actual_run_params);
    if (err != BoltError::SUCCESS) {
        print_bolt_error_details_server("Deserializing RUN PSS to RunMessageParams", err);
        return 1;
    }

    server_send_buffer_storage.clear();
    {
        PackStreamWriter run_response_writer(server_send_buffer_storage);
        err = ServerHandlers::handle_run_message(actual_run_params, run_response_writer);
        if (err != BoltError::SUCCESS) {
            // Error already printed by handler or writer
            return 1;
        }
    }
    print_bytes_server("Server sending full response stream for RUN (raw): ", server_send_buffer_storage);

    std::cout << "\nServer example finished." << std::endl;
    return 0;
}