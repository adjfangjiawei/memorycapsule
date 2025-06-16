#include <array>      // For handshake
#include <exception>  // For std::bad_alloc, std::exception
#include <iomanip>    // For std::setw, std::setfill
#include <iostream>
#include <map>
#include <memory>   // For std::shared_ptr
#include <sstream>  // For std::stringstream to simulate socket streams
#include <string>
#include <vector>

#include "boltprotocol/chunking.h"
#include "boltprotocol/handshake.h"
#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"

// Helper to print BoltError and associated reader/writer errors
void print_bolt_error_details_client(
    const std::string& context, boltprotocol::BoltError err_code, boltprotocol::PackStreamReader* reader = nullptr, boltprotocol::PackStreamWriter* writer = nullptr, boltprotocol::ChunkedReader* chunk_reader = nullptr, boltprotocol::ChunkedWriter* chunk_writer = nullptr) {
    std::cerr << "Error (Client) " << context << ": " << static_cast<int>(err_code);
    if (reader && reader->has_error() && reader->get_error() != err_code) {
        std::cerr << " (PackStreamReader specific error: " << static_cast<int>(reader->get_error()) << ")";
    }
    if (writer && writer->has_error() && writer->get_error() != err_code) {
        std::cerr << " (PackStreamWriter specific error: " << static_cast<int>(writer->get_error()) << ")";
    }
    if (chunk_reader && chunk_reader->has_error() && chunk_reader->get_error() != err_code) {
        std::cerr << " (ChunkedReader specific error: " << static_cast<int>(chunk_reader->get_error()) << ")";
    }
    if (chunk_writer && chunk_writer->has_error() && chunk_writer->get_error() != err_code) {
        std::cerr << " (ChunkedWriter specific error: " << static_cast<int>(chunk_writer->get_error()) << ")";
    }
    std::cerr << std::endl;
}

// Helper function to print a byte vector
void print_bytes_client(const std::string& prefix, const std::vector<uint8_t>& bytes) {
    std::cout << prefix;
    if (bytes.empty()) {
        std::cout << "(empty)" << std::endl;
        return;
    }
    for (uint8_t byte : bytes) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
    }
    std::cout << std::dec << " (size: " << bytes.size() << ")" << std::endl;
}

// Simulate sending a raw message (PackStream bytes) via ChunkedWriter
// and receiving a raw response (PackStream bytes) via ChunkedReader.
boltprotocol::BoltError send_and_receive_raw_message_client(
    std::stringstream& client_to_server_pipe, std::stringstream& server_to_client_pipe, const std::vector<uint8_t>& raw_message_to_send, std::vector<uint8_t>& out_raw_response_received, const std::string& message_description_for_log, bool expect_response = true) {
    using namespace boltprotocol;
    BoltError err;

    // --- Client Sends Message ---
    std::cout << "Client: Preparing to send " << message_description_for_log << "..." << std::endl;
    print_bytes_client("Client: Raw " + message_description_for_log + " to send: ", raw_message_to_send);

    // Clear client_to_server_pipe for this message (caller responsibility to manage overall pipe lifetime)
    // For this helper, we assume it's okay to clear before writing.
    if (!raw_message_to_send.empty()) {  // Only clear if we are actually sending something
        client_to_server_pipe.clear();
        client_to_server_pipe.str("");
    }

    if (!raw_message_to_send.empty()) {  // Only write if there's a message
        ChunkedWriter chunk_writer(client_to_server_pipe);
        err = chunk_writer.write_message(raw_message_to_send);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("ChunkedWriter writing " + message_description_for_log, err, nullptr, nullptr, nullptr, &chunk_writer);
            return err;
        }
        std::cout << "Client: " << message_description_for_log << " written to client_to_server_pipe (chunked)." << std::endl;
    } else if (expect_response) {  // If sending nothing but expecting response (e.g. implicit PULL after RUN)
        std::cout << "Client: Sending no explicit message, but expecting response for " << message_description_for_log << "." << std::endl;
    } else {  // Sending nothing, expecting nothing
        std::cout << "Client: No message to send and no response expected for " << message_description_for_log << "." << std::endl;
    }

    // --- Client Receiving Response (if expected) ---
    if (!expect_response) {
        std::cout << "Client: No response expected for " << message_description_for_log << "." << std::endl;
        // Even if no response expected, server_pipe might have old data; clear it.
        server_to_client_pipe.clear();
        server_to_client_pipe.str("");
        return BoltError::SUCCESS;
    }

    std::cout << "Client: Waiting for server response to " << message_description_for_log << "..." << std::endl;
    // Check if the server pipe is empty *before* trying to read.
    // Need to peek to see if there's content, as str() doesn't reflect read position.
    server_to_client_pipe.peek();  // This updates EOF state if at end
    if (server_to_client_pipe.str().empty() && server_to_client_pipe.eof()) {
        std::cout << "Client: Server_to_client_pipe is empty and at EOF. No response to read for " << message_description_for_log << "." << std::endl;
        print_bolt_error_details_client("ChunkedReader reading response to " + message_description_for_log + " (pipe was empty)", BoltError::NETWORK_ERROR);
        return BoltError::NETWORK_ERROR;
    }

    out_raw_response_received.clear();
    ChunkedReader chunk_reader(server_to_client_pipe);
    err = chunk_reader.read_message(out_raw_response_received);

    if (err != BoltError::SUCCESS) {
        // If read_message fails, it might be due to an empty pipe that wasn't caught above, or actual error.
        print_bolt_error_details_client("ChunkedReader reading response to " + message_description_for_log, err, nullptr, nullptr, &chunk_reader);
        return err;
    }
    print_bytes_client("Client: Raw response received for " + message_description_for_log + ": ", out_raw_response_received);

    // Clear the server_to_client_pipe after successful read, ready for next simulated server response.
    server_to_client_pipe.clear();
    server_to_client_pipe.str("");

    return BoltError::SUCCESS;
}

// Helper to simulate server sending a simple SUCCESS {} response
boltprotocol::BoltError simulate_server_simple_success_response(std::stringstream& server_pipe, const std::string& context_log, int64_t qid = -1) {
    using namespace boltprotocol;
    std::vector<uint8_t> raw_server_response_bytes;
    BoltError err;
    {
        PackStreamWriter ps_writer(raw_server_response_bytes);
        SuccessMessageParams success_p;
        bool prep_ok = true;
        try {
            if (qid != -1) {
                success_p.metadata.emplace("qid", Value(qid));
            }
            // Add other common success fields if needed for simulation
            // success_p.metadata.emplace("type", Value(std::string("r"))); // For query summary
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_client("Sim Srv (" + context_log + ") SUCCESS alloc", BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            std::cerr << "StdExc Sim Srv (" + context_log + ") SUCCESS: " << e_std.what() << std::endl;
            print_bolt_error_details_client("Sim Srv (" + context_log + ") SUCCESS stdexc", BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        PackStreamStructure success_pss;
        success_pss.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
        std::shared_ptr<BoltMap> meta_map_sptr;
        try {
            meta_map_sptr = std::make_shared<BoltMap>();
            meta_map_sptr->pairs = std::move(success_p.metadata);  // Move if success_p not used after
            success_pss.fields.emplace_back(Value(meta_map_sptr));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_client("Sim Srv (" + context_log + ") SUCCESS PSS alloc", BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            std::cerr << "StdExc Sim Srv (" + context_log + ") PSS: " << e_std.what() << std::endl;
            print_bolt_error_details_client("Sim Srv (" + context_log + ") PSS stdexc", BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(success_pss));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_client("Sim Srv (" + context_log + ") PSS sptr alloc", BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        } catch (const std::exception& e_std) {
            std::cerr << "StdExc Sim Srv (" + context_log + ") PSS sptr: " << e_std.what() << std::endl;
            print_bolt_error_details_client("Sim Srv (" + context_log + ") PSS sptr stdexc", BoltError::UNKNOWN_ERROR);
            return BoltError::UNKNOWN_ERROR;
        }
        if (!pss_sptr) {
            print_bolt_error_details_client("Sim Srv (" + context_log + ") PSS sptr null", BoltError::OUT_OF_MEMORY);
            return BoltError::OUT_OF_MEMORY;
        }

        err = ps_writer.write(Value(std::move(pss_sptr)));
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("Sim Srv: serializing " + context_log + " SUCCESS", err, nullptr, &ps_writer);
            return err;
        }
    }
    // "Server" puts its response into the server_pipe
    server_pipe.clear();
    server_pipe.str("");  // Clear for this specific response
    {
        ChunkedWriter server_chunk_writer(server_pipe);
        err = server_chunk_writer.write_message(raw_server_response_bytes);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("Sim Srv: chunk writing " + context_log + " SUCCESS", err, nullptr, nullptr, nullptr, &server_chunk_writer);
            return err;
        }
    }
    std::cout << "Server (Simulated): Sent " << context_log << " SUCCESS response." << std::endl;
    return BoltError::SUCCESS;
}

int main() {
    using namespace boltprotocol;

    std::cout << "Bolt Protocol Client Example (No-Exception, Integrated Handshake/Chunking/Tx)" << std::endl;
    std::cout << "-----------------------------------------------------------------------------" << std::endl;

    BoltError err = BoltError::SUCCESS;
    std::stringstream client_to_server_stream;
    std::stringstream server_to_client_stream;
    versions::Version negotiated_version;
    std::vector<uint8_t> raw_message_bytes_storage;
    std::vector<uint8_t> raw_response_bytes_storage;

    // --- 0. Perform Handshake ---
    std::cout << "\n--- Performing Handshake ---" << std::endl;
    client_to_server_stream.clear();
    client_to_server_stream.str("");  // Clear client write pipe
    server_to_client_stream.clear();
    server_to_client_stream.str("");  // Clear server write pipe (client read pipe)
    {
        std::vector<versions::Version> proposed_versions = versions::get_default_proposed_versions();
        if (proposed_versions.empty()) {
            print_bolt_error_details_client("main: proposed_versions empty", BoltError::INVALID_ARGUMENT);
            return 1;
        }

        versions::Version server_chosen_version_sim = proposed_versions[0];
        std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES> server_response_b = server_chosen_version_sim.to_handshake_bytes();
        server_to_client_stream.write(reinterpret_cast<const char*>(server_response_b.data()), HANDSHAKE_RESPONSE_SIZE_BYTES);

        err = perform_handshake(client_to_server_stream, server_to_client_stream, proposed_versions, negotiated_version);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("performing handshake", err);
            return 1;
        }
        std::cout << "Client: Handshake successful! Negotiated version: " << static_cast<int>(negotiated_version.major) << "." << static_cast<int>(negotiated_version.minor) << std::endl;
    }
    // After handshake, client_to_server_stream contains handshake request, server_to_client_stream is now empty.

    // --- 1. Client Sends HELLO Message ---
    std::cout << "\n--- Client Sending HELLO ---" << std::endl;
    raw_message_bytes_storage.clear();
    {
        PackStreamWriter ps_writer(raw_message_bytes_storage);
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
        if (!prep_ok) return 1;

        err = serialize_hello_message(hello_params, ps_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing HELLO", err, nullptr, &ps_writer);
            return 1;
        }
    }
    // Simulate Server Response to HELLO (SUCCESS)
    err = simulate_server_simple_success_response(server_to_client_stream, "HELLO");  // server_to_client_stream now primed
    if (err != BoltError::SUCCESS) return 1;
    // Client sends HELLO and gets response
    client_to_server_stream.clear();
    client_to_server_stream.str("");  // Clear client write pipe for HELLO
    err = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "HELLO");
    if (err != BoltError::SUCCESS) return 1;
    // Client deserializes the SUCCESS response for HELLO
    if (raw_response_bytes_storage.empty()) {
        print_bolt_error_details_client("HELLO resp empty", BoltError::DESERIALIZATION_ERROR);
        return 1;
    }
    SuccessMessageParams hello_success_params;
    {
        PackStreamReader hello_response_reader(raw_response_bytes_storage);
        err = deserialize_success_message(hello_response_reader, hello_success_params);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("deser HELLO SUCCESS", err, &hello_response_reader);
            return 1;
        }
    }
    std::cout << "Client: HELLO SUCCESS deserialized." << std::endl;

    // --- 2. Start a Transaction (BEGIN) ---
    std::cout << "\n--- Client Sending BEGIN ---" << std::endl;
    raw_message_bytes_storage.clear();
    {
        PackStreamWriter ps_writer(raw_message_bytes_storage);
        BeginMessageParams begin_params;  // Empty 'extra' map by default
        err = serialize_begin_message(begin_params, ps_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing BEGIN", err, nullptr, &ps_writer);
            return 1;
        }
    }
    err = simulate_server_simple_success_response(server_to_client_stream, "BEGIN");
    if (err != BoltError::SUCCESS) return 1;
    client_to_server_stream.clear();
    client_to_server_stream.str("");
    err = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "BEGIN");
    if (err != BoltError::SUCCESS) return 1;
    if (raw_response_bytes_storage.empty()) {
        print_bolt_error_details_client("BEGIN resp empty", BoltError::DESERIALIZATION_ERROR);
        return 1;
    }
    SuccessMessageParams begin_success_params;
    {
        PackStreamReader r(raw_response_bytes_storage);
        err = deserialize_success_message(r, begin_success_params);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("deser BEGIN SUCCESS", err, &r);
            return 1;
        }
    }
    std::cout << "Client: BEGIN SUCCESS deserialized." << std::endl;

    // --- 3. Run a query within the transaction (RUN + PULL) ---
    std::cout << "\n--- Client Sending RUN (in transaction) ---" << std::endl;
    raw_message_bytes_storage.clear();
    int64_t run_in_tx_qid = 2;
    {
        PackStreamWriter ps_writer(raw_message_bytes_storage);
        RunMessageParams run_params;
        bool prep_ok = true;
        try {
            run_params.cypher_query = "CREATE (a:Person {name: 'Alice'}) RETURN id(a)";
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_client("alloc RUN_IN_TX params", BoltError::OUT_OF_MEMORY);
            prep_ok = false;
        } catch (const std::exception& e) {
            std::cerr << "StdExc RUN_IN_TX params: " << e.what() << std::endl;
            print_bolt_error_details_client("prep RUN_IN_TX params", BoltError::UNKNOWN_ERROR);
            prep_ok = false;
        }
        if (!prep_ok) return 1;
        err = serialize_run_message(run_params, ps_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing RUN_IN_TX", err, nullptr, &ps_writer);
            return 1;
        }
    }
    // Simulate Server Response to RUN (SUCCESS {fields, qid})
    std::vector<uint8_t> raw_server_run_fields_response;
    {
        PackStreamWriter srv_ps_writer(raw_server_run_fields_response);
        SuccessMessageParams fields_s_p;
        bool prep_ok = true;
        try {
            auto fields_list = std::make_shared<BoltList>();
            fields_list->elements.emplace_back(Value(std::string("id(a)")));
            fields_s_p.metadata.emplace("fields", Value(fields_list));
            fields_s_p.metadata.emplace("qid", Value(run_in_tx_qid));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_client("Sim Srv: RUN_IN_TX SUCCESS fields alloc", BoltError::OUT_OF_MEMORY);
            prep_ok = false;
        } catch (const std::exception& e) {
            std::cerr << "StdExc Sim Srv: RUN_IN_TX SUCCESS fields: " << e.what() << std::endl;
            print_bolt_error_details_client("Sim Srv: RUN_IN_TX SUCCESS fields stdexc", BoltError::UNKNOWN_ERROR);
            prep_ok = false;
        }
        if (!prep_ok) return 1;

        PackStreamStructure pss;
        pss.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
        std::shared_ptr<BoltMap> meta;
        bool pss_prep_ok = true;
        try {
            meta = std::make_shared<BoltMap>();
            meta->pairs = std::move(fields_s_p.metadata);
            pss.fields.emplace_back(Value(meta));
        } catch (...) {
            pss_prep_ok = false;
        }
        if (!pss_prep_ok) return 1;

        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pss));
        } catch (...) {
            return 1;
        }
        if (!pss_sptr) return 1;

        err = srv_ps_writer.write(Value(pss_sptr));
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("Sim Srv: serializing RUN_IN_TX SUCCESS fields", err);
            return 1;
        }
    }
    server_to_client_stream.clear();
    server_to_client_stream.str("");
    {
        ChunkedWriter srv_c_writer(server_to_client_stream);
        err = srv_c_writer.write_message(raw_server_run_fields_response);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("Sim Srv: chunking RUN_IN_TX SUCCESS fields", err);
            return 1;
        }
    }

    client_to_server_stream.clear();
    client_to_server_stream.str("");
    err = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "RUN_IN_TX");
    if (err != BoltError::SUCCESS) return 1;
    if (raw_response_bytes_storage.empty()) {
        print_bolt_error_details_client("RUN_IN_TX resp empty", BoltError::DESERIALIZATION_ERROR);
        return 1;
    }
    SuccessMessageParams run_in_tx_success_params;
    {
        PackStreamReader r(raw_response_bytes_storage);
        err = deserialize_success_message(r, run_in_tx_success_params);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("deser RUN_IN_TX SUCCESS", err, &r);
            return 1;
        }
    }
    std::cout << "Client: RUN_IN_TX SUCCESS (fields) deserialized." << std::endl;

    // Client sends PULL to get results
    std::cout << "\n--- Client Sending PULL (in transaction) ---" << std::endl;
    raw_message_bytes_storage.clear();
    {
        PackStreamWriter ps_writer(raw_message_bytes_storage);
        PullMessageParams pull_params;
        bool prep_ok = true;
        try {
            pull_params.n = static_cast<int64_t>(-1);
            pull_params.qid = run_in_tx_qid;
        } catch (const std::bad_alloc&) {
            print_bolt_error_details_client("alloc PULL params", BoltError::OUT_OF_MEMORY);
            prep_ok = false;
        } catch (const std::exception& e) {
            std::cerr << "StdExc PULL params: " << e.what() << std::endl;
            print_bolt_error_details_client("prep PULL params", BoltError::UNKNOWN_ERROR);
            prep_ok = false;
        }
        if (!prep_ok) return 1;
        err = serialize_pull_message(pull_params, ps_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing PULL", err, nullptr, &ps_writer);
            return 1;
        }
    }
    // Simulate Server Response to PULL: one RECORD
    std::vector<uint8_t> raw_server_record_response;
    {
        PackStreamWriter srv_ps_writer(raw_server_record_response);
        RecordMessageParams rec_p;
        bool prep_ok = true;
        try {
            rec_p.fields.emplace_back(Value(static_cast<int64_t>(12345)));
        } catch (...) {
            prep_ok = false;
        }
        if (!prep_ok) return 1;
        PackStreamStructure pss;
        pss.tag = static_cast<uint8_t>(MessageTag::RECORD);
        std::shared_ptr<BoltList> list_sptr;
        bool pss_prep_ok = true;
        try {
            list_sptr = std::make_shared<BoltList>();
            list_sptr->elements = std::move(rec_p.fields);
            pss.fields.emplace_back(Value(list_sptr));
        } catch (...) {
            pss_prep_ok = false;
        }
        if (!pss_prep_ok) return 1;
        std::shared_ptr<PackStreamStructure> pss_sptr;
        try {
            pss_sptr = std::make_shared<PackStreamStructure>(std::move(pss));
        } catch (...) {
            return 1;
        }
        if (!pss_sptr) return 1;
        err = srv_ps_writer.write(Value(pss_sptr));
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("Sim Srv: serializing RECORD", err);
            return 1;
        }
    }
    server_to_client_stream.clear();
    server_to_client_stream.str("");
    {
        ChunkedWriter srv_c_writer(server_to_client_stream);
        err = srv_c_writer.write_message(raw_server_record_response);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("Sim Srv: chunking RECORD", err);
            return 1;
        }
    }

    client_to_server_stream.clear();
    client_to_server_stream.str("");
    err = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "PULL (for RECORD)");
    if (err != BoltError::SUCCESS) return 1;
    if (raw_response_bytes_storage.empty()) {
        print_bolt_error_details_client("PULL RECORD resp empty", BoltError::DESERIALIZATION_ERROR);
        return 1;
    }
    RecordMessageParams rec_msg_params;
    {
        PackStreamReader r(raw_response_bytes_storage);
        err = deserialize_record_message(r, rec_msg_params);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("deser RECORD", err, &r);
            return 1;
        }
    }
    std::cout << "Client: RECORD deserialized." << std::endl;

    // Server sends SUCCESS (summary for PULL)
    err = simulate_server_simple_success_response(server_to_client_stream, "PULL summary", run_in_tx_qid);
    if (err != BoltError::SUCCESS) return 1;
    raw_message_bytes_storage.clear();
    client_to_server_stream.clear();
    client_to_server_stream.str("");  // No client message, just receiving
    err = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "PULL (for summary SUCCESS)", true);
    if (err != BoltError::SUCCESS) return 1;
    if (raw_response_bytes_storage.empty()) {
        print_bolt_error_details_client("PULL summary resp empty", BoltError::DESERIALIZATION_ERROR);
        return 1;
    }
    SuccessMessageParams pull_summary_params;
    {
        PackStreamReader r(raw_response_bytes_storage);
        err = deserialize_success_message(r, pull_summary_params);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("deser PULL SUCCESS", err, &r);
            return 1;
        }
    }
    std::cout << "Client: PULL summary SUCCESS deserialized." << std::endl;

    // --- 4. Commit the Transaction ---
    std::cout << "\n--- Client Sending COMMIT ---" << std::endl;
    raw_message_bytes_storage.clear();
    {
        PackStreamWriter ps_writer(raw_message_bytes_storage);
        err = serialize_commit_message(ps_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing COMMIT", err, nullptr, &ps_writer);
            return 1;
        }
    }
    err = simulate_server_simple_success_response(server_to_client_stream, "COMMIT");
    if (err != BoltError::SUCCESS) return 1;
    client_to_server_stream.clear();
    client_to_server_stream.str("");
    err = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "COMMIT");
    if (err != BoltError::SUCCESS) return 1;
    if (raw_response_bytes_storage.empty()) {
        print_bolt_error_details_client("COMMIT resp empty", BoltError::DESERIALIZATION_ERROR);
        return 1;
    }
    SuccessMessageParams commit_success_params;
    {
        PackStreamReader r(raw_response_bytes_storage);
        err = deserialize_success_message(r, commit_success_params);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("deser COMMIT SUCCESS", err, &r);
            return 1;
        }
    }
    std::cout << "Client: COMMIT SUCCESS deserialized." << std::endl;

    // --- 5. Client Sends GOODBYE ---
    std::cout << "\n--- Client Sending GOODBYE ---" << std::endl;
    raw_message_bytes_storage.clear();
    {
        PackStreamWriter ps_writer(raw_message_bytes_storage);
        err = serialize_goodbye_message(ps_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details_client("serializing GOODBYE", err, nullptr, &ps_writer);
            return 1;
        }
    }
    // GOODBYE is one-way. Server might close or send nothing.
    server_to_client_stream.clear();
    server_to_client_stream.str("");
    client_to_server_stream.clear();
    client_to_server_stream.str("");
    err = send_and_receive_raw_message_client(client_to_server_stream, server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "GOODBYE", false);
    if (err != BoltError::SUCCESS) {
        return 1;
    }
    std::cout << "Client: GOODBYE sent." << std::endl;

    std::cout << "\nClient example finished successfully." << std::endl;
    return 0;
}