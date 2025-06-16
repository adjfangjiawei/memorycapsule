#include "client_example_utils.h"

// Helper to print BoltError and associated reader/writer errors
void print_bolt_error_details_client(const std::string& context, boltprotocol::BoltError err_code, boltprotocol::PackStreamReader* reader, boltprotocol::PackStreamWriter* writer, boltprotocol::ChunkedReader* chunk_reader, boltprotocol::ChunkedWriter* chunk_writer) {
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
    std::stringstream& client_to_server_pipe, std::stringstream& server_to_client_pipe, const std::vector<uint8_t>& raw_message_to_send, std::vector<uint8_t>& out_raw_response_received, const std::string& message_description_for_log, bool expect_response) {
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
boltprotocol::BoltError simulate_server_simple_success_response(std::stringstream& server_pipe, const std::string& context_log, int64_t qid) {
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