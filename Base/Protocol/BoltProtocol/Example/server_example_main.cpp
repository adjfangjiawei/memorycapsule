#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "boltprotocol/bolt_errors_versions.h"  // For direct use of versions::V5_X
#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "server_example_handlers.h"
#include "server_example_utils.h"

// Helper to simulate client sending a HELLO message
boltprotocol::BoltError simulate_client_hello(const boltprotocol::versions::Version& client_simulated_target_version, std::vector<uint8_t>& out_raw_bytes) {
    using namespace boltprotocol;
    out_raw_bytes.clear();
    PackStreamWriter client_hello_writer(out_raw_bytes);

    HelloMessageParams client_hello_params;
    bool client_prep_ok = true;
    try {
        client_hello_params.user_agent = "MyExampleCppClient/1.0 (Simulated)";

        if (client_simulated_target_version < versions::V5_1) {
            client_hello_params.auth_scheme = "basic";
            client_hello_params.auth_principal = "neo4j";
            client_hello_params.auth_credentials = "password";
        }
        if (!(client_simulated_target_version < versions::V5_3)) {  // client_simulated_target_version >= V5_3
            HelloMessageParams::BoltAgentInfo agent_info;
            agent_info.product = "SimulatedClientDriver/0.5";
            client_hello_params.bolt_agent = agent_info;
        }
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
    if (!client_prep_ok) return BoltError::UNKNOWN_ERROR;

    BoltError err = serialize_hello_message(client_hello_params, client_hello_writer, client_simulated_target_version);
    if (err != BoltError::SUCCESS) {
        print_bolt_error_details_server("client sim serializing HELLO", err, nullptr, &client_hello_writer);
    }
    return err;
}

// Helper to simulate client sending a RUN message
boltprotocol::BoltError simulate_client_run(const boltprotocol::versions::Version& client_simulated_target_version,  // Added version
                                            std::vector<uint8_t>& out_raw_bytes) {
    using namespace boltprotocol;
    out_raw_bytes.clear();
    PackStreamWriter client_run_writer(out_raw_bytes);
    RunMessageParams client_run_params;
    bool client_run_prep_ok = true;
    try {
        client_run_params.cypher_query = "MATCH (n) RETURN n.name AS name LIMIT $limit";
        client_run_params.parameters.emplace("limit", Value(static_cast<int64_t>(5)));
        // Example: Populate some typed extra fields if simulating a client that sends them
        if (client_simulated_target_version.major >= 4) {
            client_run_params.db = "system";
        }
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

    // Pass the target version for RUN serialization
    BoltError err = serialize_run_message(client_run_params, client_run_writer, client_simulated_target_version);
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

    versions::Version server_assumed_negotiated_version = versions::V5_3;  // Example version server operates as

    std::cout << "\nServer expecting HELLO message (simulating client targeting v" << static_cast<int>(server_assumed_negotiated_version.major) << "." << static_cast<int>(server_assumed_negotiated_version.minor) << ") ..." << std::endl;

    err = simulate_client_hello(server_assumed_negotiated_version, server_receive_buffer_storage);
    if (err != BoltError::SUCCESS) return 1;
    print_bytes_server("Server received bytes for HELLO (raw): ", server_receive_buffer_storage);

    HelloMessageParams actual_hello_params;
    {
        PackStreamReader hello_reader(server_receive_buffer_storage);
        err = deserialize_hello_message_request(hello_reader, actual_hello_params, server_assumed_negotiated_version);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server deserializing HELLO request", err, &hello_reader);
            return 1;
        }
    }
    std::cout << "Server: HELLO message structure received and parsed." << std::endl;
    std::cout << "  User Agent from HELLO: " << actual_hello_params.user_agent << std::endl;
    if (actual_hello_params.bolt_agent.has_value()) {
        std::cout << "  Bolt Agent Product: " << actual_hello_params.bolt_agent.value().product << std::endl;
    }

    server_send_buffer_storage.clear();
    {
        PackStreamWriter success_hello_writer(server_send_buffer_storage);
        err = ServerHandlers::handle_hello_message(actual_hello_params, success_hello_writer, server_assumed_negotiated_version);
        if (err != BoltError::SUCCESS) {
            return 1;
        }
    }
    print_bytes_server("Server sending SUCCESS (for HELLO) (raw): ", server_send_buffer_storage);

    // === Stage 2: Client sends RUN, Server processes and responds ===
    std::cout << "\nServer expecting RUN message..." << std::endl;
    // Client also simulates RUN targeting the assumed negotiated version
    err = simulate_client_run(server_assumed_negotiated_version, server_receive_buffer_storage);
    if (err != BoltError::SUCCESS) return 1;
    print_bytes_server("Server received bytes for RUN (raw): ", server_receive_buffer_storage);

    RunMessageParams actual_run_params;
    {
        PackStreamReader run_reader(server_receive_buffer_storage);
        // Server deserializes RUN based on the version it negotiated
        err = deserialize_run_message_request(run_reader, actual_run_params, server_assumed_negotiated_version);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_server("Server deserializing RUN request", err, &run_reader);
            return 1;
        }
    }
    std::cout << "Server: RUN message structure received and parsed." << std::endl;
    if (actual_run_params.db.has_value()) {
        std::cout << "  RUN request for database: " << actual_run_params.db.value() << std::endl;
    }

    server_send_buffer_storage.clear();
    {
        PackStreamWriter run_response_writer(server_send_buffer_storage);
        // Pass the negotiated version to the handler if it needs to make version-specific decisions
        // For now, handle_run_message doesn't use it, but good practice.
        err = ServerHandlers::handle_run_message(actual_run_params, run_response_writer /*, server_assumed_negotiated_version */);
        if (err != BoltError::SUCCESS) {
            return 1;
        }
    }
    print_bytes_server("Server sending full response stream for RUN (raw): ", server_send_buffer_storage);

    std::cout << "\nServer example finished." << std::endl;
    return 0;
}