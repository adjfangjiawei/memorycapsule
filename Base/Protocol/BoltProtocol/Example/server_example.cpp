#include <iomanip>  // For std::setw, std::setfill
#include <iostream>
#include <map>
#include <memory>  // For std::make_shared, std::shared_ptr
#include <string>
#include <variant>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol_example_server {

    using namespace ::boltprotocol;

    void print_bytes_server(const std::string& prefix, const std::vector<uint8_t>& bytes) {
        std::cout << prefix;
        for (uint8_t byte : bytes) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
        }
        std::cout << std::dec << std::endl;
    }

    void process_run_query(const RunMessageParams& run_params, PackStreamWriter& response_writer) {
        std::cout << "  Server processing RUN query: " << run_params.cypher_query << std::endl;
        auto limit_it = run_params.parameters.find("limit");
        if (limit_it != run_params.parameters.end()) {
            if (auto* limit_val = std::get_if<int64_t>(&(limit_it->second))) {
                std::cout << "    With limit: " << *limit_val << std::endl;
            }
        }

        BoltError err;

        // 1. Send SUCCESS for RUN (contains field names)
        SuccessMessageParams run_success_params;
        auto fields_list_sptr = std::make_shared<BoltList>();
        fields_list_sptr->elements.emplace_back(Value(std::string("name")));
        run_success_params.metadata.emplace("fields", Value(std::move(fields_list_sptr)));

        PackStreamStructure run_success_struct_obj;  // Create object
        run_success_struct_obj.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
        auto run_success_meta_map_sptr = std::make_shared<BoltMap>();
        run_success_meta_map_sptr->pairs = std::move(run_success_params.metadata);
        run_success_struct_obj.fields.emplace_back(Value(std::move(run_success_meta_map_sptr)));

        err = response_writer.write(Value(std::make_shared<PackStreamStructure>(std::move(run_success_struct_obj))));
        if (err != BoltError::SUCCESS) {
            std::cerr << "  Server error serializing SUCCESS for RUN: " << static_cast<int>(err) << std::endl;
            return;
        }
        std::cout << "  Server sent SUCCESS for RUN (with fields)." << std::endl;

        // 2. Send RECORD messages (dummy data)
        for (int i = 0; i < 2; ++i) {
            RecordMessageParams record_params;
            record_params.fields.emplace_back(Value(std::string("Node " + std::to_string(i))));

            PackStreamStructure record_struct_obj;  // Create object
            record_struct_obj.tag = static_cast<uint8_t>(MessageTag::RECORD);
            auto record_fields_list_sptr = std::make_shared<BoltList>();
            record_fields_list_sptr->elements = std::move(record_params.fields);
            record_struct_obj.fields.emplace_back(Value(std::move(record_fields_list_sptr)));

            err = response_writer.write(Value(std::make_shared<PackStreamStructure>(std::move(record_struct_obj))));
            if (err != BoltError::SUCCESS) {
                std::cerr << "  Server error serializing RECORD " << i << ": " << static_cast<int>(err) << std::endl;
                return;
            }
            std::cout << "  Server sent RECORD " << i << "." << std::endl;
        }

        // 3. Send final SUCCESS (summary)
        SuccessMessageParams summary_success_params;
        summary_success_params.metadata.emplace("type", Value(std::string("r")));

        PackStreamStructure summary_struct_obj;  // Create object
        summary_struct_obj.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
        auto summary_meta_map_sptr = std::make_shared<BoltMap>();
        summary_meta_map_sptr->pairs = std::move(summary_success_params.metadata);
        summary_struct_obj.fields.emplace_back(Value(std::move(summary_meta_map_sptr)));

        err = response_writer.write(Value(std::make_shared<PackStreamStructure>(std::move(summary_struct_obj))));
        if (err != BoltError::SUCCESS) {
            std::cerr << "  Server error serializing SUCCESS summary: " << static_cast<int>(err) << std::endl;
            return;
        }
        std::cout << "  Server sent SUCCESS summary." << std::endl;
    }

}  // namespace boltprotocol_example_server

int main() {
    using namespace boltprotocol_example_server;
    using namespace boltprotocol;

    std::cout << "Bolt Protocol Server Example" << std::endl;
    std::cout << "----------------------------" << std::endl;

    std::vector<uint8_t> server_receive_buffer;
    std::vector<uint8_t> server_send_buffer;

    // === Stage 1: Client sends HELLO, Server responds SUCCESS ===
    std::cout << "\nServer expecting HELLO message..." << std::endl;
    {  // Simulate client sending HELLO
        HelloMessageParams client_hello_params;
        client_hello_params.extra_auth_tokens.emplace("user_agent", Value(std::string("MyExampleCppClient/1.0")));
        client_hello_params.extra_auth_tokens.emplace("scheme", Value(std::string("basic")));
        client_hello_params.extra_auth_tokens.emplace("principal", Value(std::string("neo4j")));
        client_hello_params.extra_auth_tokens.emplace("credentials", Value(std::string("password")));

        PackStreamWriter temp_writer(server_receive_buffer);
        serialize_hello_message(client_hello_params, temp_writer);
    }
    print_bytes_server("Server received bytes for HELLO: ", server_receive_buffer);

    PackStreamReader hello_reader(server_receive_buffer);
    Value received_value_hello;
    BoltError err = hello_reader.read(received_value_hello);
    if (err != BoltError::SUCCESS) {
        std::cerr << "Server: Error reading HELLO structure. Error: " << static_cast<int>(err) << std::endl;
        if (hello_reader.has_error()) std::cerr << "  Reader error: " << static_cast<int>(hello_reader.get_error()) << std::endl;
        return 1;
    }

    if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(received_value_hello)) {  // MODIFIED
        std::cerr << "Server: Received HELLO message is not a structure as expected." << std::endl;
        return 1;
    }

    std::shared_ptr<PackStreamStructure> hello_struct_sptr = std::get<std::shared_ptr<PackStreamStructure>>(std::move(received_value_hello));  // MODIFIED
    if (!hello_struct_sptr || static_cast<MessageTag>(hello_struct_sptr->tag) != MessageTag::HELLO) {
        std::cerr << "Server: Received message is not HELLO." << std::endl;
        return 1;
    }
    std::cout << "Server: HELLO message structure received." << std::endl;
    // ... further parsing of hello_struct_sptr->fields[0] (the metadata map) would go here ...

    server_send_buffer.clear();
    PackStreamWriter success_hello_writer(server_send_buffer);
    SuccessMessageParams success_for_hello_params;
    success_for_hello_params.metadata.emplace("connection_id", Value(std::string("server-conn-xyz")));
    success_for_hello_params.metadata.emplace("server", Value(std::string("MyExampleBoltServer/0.1")));

    PackStreamStructure success_pss_for_hello_obj;  // Create object
    success_pss_for_hello_obj.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
    auto success_meta_map_for_hello_sptr = std::make_shared<BoltMap>();
    success_meta_map_for_hello_sptr->pairs = std::move(success_for_hello_params.metadata);
    success_pss_for_hello_obj.fields.emplace_back(Value(std::move(success_meta_map_for_hello_sptr)));
    err = success_hello_writer.write(Value(std::make_shared<PackStreamStructure>(std::move(success_pss_for_hello_obj))));

    if (err != BoltError::SUCCESS) {
        std::cerr << "Server: Error serializing SUCCESS for HELLO: " << static_cast<int>(err) << std::endl;
        return 1;
    }
    print_bytes_server("Server sending SUCCESS (for HELLO): ", server_send_buffer);
    server_receive_buffer.clear();

    // === Stage 2: Client sends RUN, Server responds with SUCCESS, RECORD(s), SUCCESS ===
    std::cout << "\nServer expecting RUN message..." << std::endl;
    {  // Simulate client sending RUN
        RunMessageParams client_run_params;
        client_run_params.cypher_query = "MATCH (n) RETURN n.name AS name LIMIT $limit";
        client_run_params.parameters.emplace("limit", Value(static_cast<int64_t>(5)));

        PackStreamWriter temp_writer(server_receive_buffer);
        serialize_run_message(client_run_params, temp_writer);
    }
    print_bytes_server("Server received bytes for RUN: ", server_receive_buffer);

    PackStreamReader run_reader(server_receive_buffer);
    Value received_value_run;
    err = run_reader.read(received_value_run);
    if (err != BoltError::SUCCESS) {
        std::cerr << "Server: Error reading RUN structure. Error: " << static_cast<int>(err) << std::endl;
        if (run_reader.has_error()) std::cerr << "  Reader error: " << static_cast<int>(run_reader.get_error()) << std::endl;
        return 1;
    }
    if (!std::holds_alternative<std::shared_ptr<PackStreamStructure>>(received_value_run)) {  // MODIFIED
        std::cerr << "Server: Received RUN message is not a structure as expected." << std::endl;
        return 1;
    }
    std::shared_ptr<PackStreamStructure> run_struct_sptr = std::get<std::shared_ptr<PackStreamStructure>>(std::move(received_value_run));  // MODIFIED
    if (!run_struct_sptr || static_cast<MessageTag>(run_struct_sptr->tag) != MessageTag::RUN) {
        std::cerr << "Server: Received message is not RUN." << std::endl;
        return 1;
    }
    std::cout << "Server: RUN message structure received." << std::endl;

    RunMessageParams actual_run_params;
    // Deserialize RUN PSS into actual_run_params
    // Important: check if fields vector is large enough before accessing elements
    if (run_struct_sptr->fields.size() >= 1 && std::holds_alternative<std::string>(run_struct_sptr->fields[0])) {
        actual_run_params.cypher_query = std::get<std::string>(std::move(run_struct_sptr->fields[0]));
    }
    if (run_struct_sptr->fields.size() >= 2 && std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct_sptr->fields[1])) {  // MODIFIED
        auto params_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_sptr->fields[1]));                       // MODIFIED
        if (params_map_sptr) {
            actual_run_params.parameters = std::move(params_map_sptr->pairs);
        }
    }
    if (run_struct_sptr->fields.size() >= 3 && std::holds_alternative<std::shared_ptr<BoltMap>>(run_struct_sptr->fields[2])) {  // MODIFIED
        auto extra_map_sptr = std::get<std::shared_ptr<BoltMap>>(std::move(run_struct_sptr->fields[2]));                        // MODIFIED
        if (extra_map_sptr) {
            actual_run_params.extra_metadata = std::move(extra_map_sptr->pairs);
        }
    }

    server_send_buffer.clear();
    PackStreamWriter run_response_writer(server_send_buffer);
    process_run_query(actual_run_params, run_response_writer);

    print_bytes_server("Server sending full response stream for RUN: ", server_send_buffer);

    std::cout << "\nServer example finished." << std::endl;
    return 0;
}