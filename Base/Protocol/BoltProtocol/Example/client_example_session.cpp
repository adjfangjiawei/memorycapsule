#include "client_example_session.h"

#include "boltprotocol/message_defs.h"  // For get_default_proposed_versions

namespace {  // Anonymous namespace for implementation details or helpers local to this file

    boltprotocol::BoltError prepare_hello_message_bytes(const boltprotocol::versions::Version& negotiated_version, std::vector<uint8_t>& out_bytes) {
        using namespace boltprotocol;
        out_bytes.clear();  // Ensure clean buffer
        PackStreamWriter ps_writer(out_bytes);
        HelloMessageParams hello_params;
        bool prep_ok = true;
        try {
            std::string user_agent = DEFAULT_USER_AGENT_FORMAT_STRING + " (Bolt " + std::to_string(negotiated_version.major) + "." + std::to_string(negotiated_version.minor) + ")";
            hello_params.extra_auth_tokens.emplace("user_agent", Value(user_agent));
            hello_params.extra_auth_tokens.emplace("scheme", Value(std::string("basic")));
            hello_params.extra_auth_tokens.emplace("principal", Value(std::string("neo4j")));
            hello_params.extra_auth_tokens.emplace("credentials", Value(std::string("password")));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_client("alloc HELLO params", BoltError::OUT_OF_MEMORY);
            prep_ok = false;
        } catch (const std::exception& e) {
            std::cerr << "StdExc HELLO params: " << e.what() << std::endl;
            print_bolt_error_details_client("prep HELLO params", BoltError::UNKNOWN_ERROR);
            prep_ok = false;
        }
        if (!prep_ok) return BoltError::UNKNOWN_ERROR;  // Or specific error

        BoltError err = serialize_hello_message(hello_params, ps_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing HELLO", err, nullptr, &ps_writer);
        }
        return err;
    }

}  // anonymous namespace

boltprotocol::BoltError ClientSession::perform_handshake_sequence() {
    using namespace boltprotocol;
    client_to_server_stream.clear();
    client_to_server_stream.str("");
    server_to_client_stream.clear();
    server_to_client_stream.str("");

    std::vector<versions::Version> proposed_versions = versions::get_default_proposed_versions();
    if (proposed_versions.empty()) {
        print_bolt_error_details_client("perform_handshake_sequence: proposed_versions empty", BoltError::INVALID_ARGUMENT);
        last_error = BoltError::INVALID_ARGUMENT;
        return last_error;
    }

    // Simulate server choosing the first proposed version
    versions::Version server_chosen_version_sim = proposed_versions[0];
    std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES> server_response_b = server_chosen_version_sim.to_handshake_bytes();
    server_to_client_stream.write(reinterpret_cast<const char*>(server_response_b.data()), HANDSHAKE_RESPONSE_SIZE_BYTES);
    server_to_client_stream.seekg(0);  // Rewind server stream for reading by perform_handshake

    last_error = perform_handshake(client_to_server_stream, server_to_client_stream, proposed_versions, negotiated_version);
    if (last_error != BoltError::SUCCESS) {
        print_bolt_error_details_client("performing handshake", last_error);
        return last_error;
    }
    std::cout << "Client: Handshake successful! Negotiated version: " << static_cast<int>(negotiated_version.major) << "." << static_cast<int>(negotiated_version.minor) << std::endl;
    return BoltError::SUCCESS;
}

boltprotocol::BoltError ClientSession::send_hello_sequence() {
    using namespace boltprotocol;
    std::vector<uint8_t> raw_message_bytes_storage;
    std::vector<uint8_t> raw_response_bytes_storage;

    last_error = prepare_hello_message_bytes(negotiated_version, raw_message_bytes_storage);
    if (last_error != BoltError::SUCCESS) return last_error;

    last_error = simulate_server_simple_success_response(server_to_client_stream, "HELLO");
    if (last_error != BoltError::SUCCESS) return last_error;

    // client_to_server_stream will be cleared by send_and_receive_raw_message_client
    last_error = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "HELLO");
    if (last_error != BoltError::SUCCESS) return last_error;

    if (raw_response_bytes_storage.empty()) {
        print_bolt_error_details_client("HELLO resp empty", BoltError::DESERIALIZATION_ERROR);
        last_error = BoltError::DESERIALIZATION_ERROR;
        return last_error;
    }
    SuccessMessageParams hello_success_params;
    {
        PackStreamReader hello_response_reader(raw_response_bytes_storage);
        last_error = deserialize_success_message(hello_response_reader, hello_success_params);
        if (last_error != BoltError::SUCCESS) {
            print_bolt_error_details_client("deser HELLO SUCCESS", last_error, &hello_response_reader);
            return last_error;
        }
    }
    std::cout << "Client: HELLO SUCCESS deserialized." << std::endl;
    // Example: Access connection_id from metadata if server sent it
    // auto it = hello_success_params.metadata.find("connection_id");
    // if (it != hello_success_params.metadata.end() && std::holds_alternative<std::string>(it->second)) {
    //    std::cout << "Client: Connection ID: " << std::get<std::string>(it->second) << std::endl;
    // }
    return BoltError::SUCCESS;
}

boltprotocol::BoltError ClientSession::send_goodbye_sequence() {
    using namespace boltprotocol;
    std::vector<uint8_t> raw_message_bytes_storage;
    std::vector<uint8_t> raw_response_bytes_storage;  // Not used for GOODBYE response

    {
        PackStreamWriter ps_writer(raw_message_bytes_storage);
        last_error = serialize_goodbye_message(ps_writer);
        if (last_error != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing GOODBYE", last_error, nullptr, &ps_writer);
            return last_error;
        }
    }

    // GOODBYE is one-way, no response expected from server typically.
    // send_and_receive_raw_message_client handles expect_response = false
    last_error = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "GOODBYE", false);
    if (last_error != BoltError::SUCCESS) {
        // Error already printed by send_and_receive
        return last_error;
    }
    std::cout << "Client: GOODBYE sent." << std::endl;
    return BoltError::SUCCESS;
}