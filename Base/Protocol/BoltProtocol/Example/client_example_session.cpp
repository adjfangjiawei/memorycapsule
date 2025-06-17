// Base/Protocol/BoltProtocol/Example/client_example_session.cpp
#include "client_example_session.h"

#include <array>  // For std::array in manual handshake simulation

#include "boltprotocol/bolt_errors_versions.h"
#include "boltprotocol/message_defs.h"

// Anonymous namespace for implementation details or helpers local to this file
namespace {

    boltprotocol::BoltError prepare_hello_message_bytes(const boltprotocol::versions::Version& target_version, std::vector<uint8_t>& out_bytes) {
        using namespace boltprotocol;
        using versions::V5_1;
        using versions::V5_3;

        out_bytes.clear();
        PackStreamWriter ps_writer(out_bytes);

        HelloMessageParams hello_params;
        bool prep_ok = true;
        try {
            hello_params.user_agent = DEFAULT_USER_AGENT_FORMAT_STRING + " (Bolt " + std::to_string(target_version.major) + "." + std::to_string(target_version.minor) + ")";

            if (target_version < V5_1) {
                hello_params.auth_scheme = "basic";
                hello_params.auth_principal = "neo4j";
                hello_params.auth_credentials = "password";
            }

            if (target_version == V5_3 || !(target_version < V5_3)) {  // Equivalent to target_version >= V5_3
                HelloMessageParams::BoltAgentInfo agent_info;
                agent_info.product = "MyExampleClientLib/0.1";
                agent_info.platform = "Cpp/LinuxGeneric";
                hello_params.bolt_agent = agent_info;
            }

        } catch (const std::bad_alloc&) {
            print_bolt_error_details_client("alloc HELLO params", BoltError::OUT_OF_MEMORY);
            prep_ok = false;
        } catch (const std::exception& e) {
            std::cerr << "StdExc HELLO params: " << e.what() << std::endl;
            print_bolt_error_details_client("prep HELLO params", BoltError::UNKNOWN_ERROR);
            prep_ok = false;
        }
        if (!prep_ok) return BoltError::UNKNOWN_ERROR;

        BoltError err = serialize_hello_message(hello_params, ps_writer, target_version);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing HELLO", err, nullptr, &ps_writer);
        }
        return err;
    }

}  // anonymous namespace

boltprotocol::BoltError ClientSession::perform_handshake_sequence() {
    using namespace boltprotocol;
    client_to_server_stream.clear();  // Clear flags
    client_to_server_stream.str("");  // Clear content
    server_to_client_stream.clear();  // Clear flags
    server_to_client_stream.str("");  // Clear content

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
    server_to_client_stream.seekg(0);  // Rewind for client to read

    // For std::stringstream, we need to pass them directly to a perform_handshake
    // that is overloaded or templated to accept std::istream and std::ostream.
    // The current template `perform_handshake(SyncReadWriteStream& stream, ...)`
    // will not work directly with two separate std::stringstream.
    // The previous attempt to call it with two std::stringstream instances was due to a
    // misunderstanding of the template after it was changed from (ostream, istream, ...) form.

    // Since client_example uses std::stringstream, and perform_handshake is now
    // templated for a single SyncReadWriteStream (which std::stringstream does not model
    // in the way Boost.ASIO sockets do, e.g. no write_some/read_some for asio::write/read free functions),
    // we must manually implement the handshake byte exchange for the example.
    // OR, we could create a simple wrapper for std::stringstream that *does* satisfy
    // SyncReadWriteStream, but that's more involved for just an example.

    std::array<uint8_t, HANDSHAKE_REQUEST_SIZE_BYTES> handshake_request_bytes_content;
    last_error = build_handshake_request(proposed_versions, handshake_request_bytes_content);
    if (last_error != BoltError::SUCCESS) {
        print_bolt_error_details_client("Building handshake request for example", last_error);
        return last_error;
    }

    client_to_server_stream.write(reinterpret_cast<const char*>(handshake_request_bytes_content.data()), HANDSHAKE_REQUEST_SIZE_BYTES);
    if (client_to_server_stream.fail()) {
        last_error = BoltError::NETWORK_ERROR;
        print_bolt_error_details_client("Example: Writing handshake to client_to_server_stream", last_error);
        return last_error;
    }
    client_to_server_stream.flush();  // Ensure it's "sent"

    std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES> server_response_bytes_read;
    server_to_client_stream.read(reinterpret_cast<char*>(server_response_bytes_read.data()), HANDSHAKE_RESPONSE_SIZE_BYTES);
    if (server_to_client_stream.fail() || static_cast<size_t>(server_to_client_stream.gcount()) != HANDSHAKE_RESPONSE_SIZE_BYTES) {
        last_error = BoltError::NETWORK_ERROR;
        print_bolt_error_details_client("Example: Reading handshake response from server_to_client_stream", last_error);
        return last_error;
    }

    last_error = parse_handshake_response(server_response_bytes_read, negotiated_version);
    if (last_error != BoltError::SUCCESS) {
        print_bolt_error_details_client("Example: Parsing handshake response", last_error);
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
    auto it_conn_id = hello_success_params.metadata.find("connection_id");
    if (it_conn_id != hello_success_params.metadata.end()) {
        if (const auto* str_val = std::get_if<std::string>(&(it_conn_id->second))) {
            std::cout << "Client: Received connection_id: " << *str_val << std::endl;
        }
    }
    auto it_server_agent = hello_success_params.metadata.find("server");
    if (it_server_agent != hello_success_params.metadata.end()) {
        if (const auto* str_val = std::get_if<std::string>(&(it_server_agent->second))) {
            std::cout << "Client: Server agent: " << *str_val << std::endl;
        }
    }
    return BoltError::SUCCESS;
}

boltprotocol::BoltError ClientSession::send_goodbye_sequence() {
    using namespace boltprotocol;
    std::vector<uint8_t> raw_message_bytes_storage;
    std::vector<uint8_t> raw_response_bytes_storage;

    {
        PackStreamWriter ps_writer(raw_message_bytes_storage);
        last_error = serialize_goodbye_message(ps_writer);
        if (last_error != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing GOODBYE", last_error, nullptr, &ps_writer);
            return last_error;
        }
    }
    last_error = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "GOODBYE", false);
    if (last_error != BoltError::SUCCESS) {
        return last_error;
    }
    std::cout << "Client: GOODBYE sent." << std::endl;
    return BoltError::SUCCESS;
}