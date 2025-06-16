#include <array>      // For handshake example (if it were fully integrated)
#include <exception>  // For std::bad_alloc, std::exception
#include <iomanip>    // For std::setw, std::setfill
#include <iostream>
#include <map>
#include <memory>  // For std::shared_ptr
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
// #include "boltprotocol/handshake.h" // Uncomment if handshake example is fully integrated
// #include "boltprotocol/chunking.h"  // Uncomment if chunking example is fully integrated

// Helper function to print a byte vector (for debugging)
void print_bytes(const std::string& prefix, const std::vector<uint8_t>& bytes) {
    std::cout << prefix;
    for (uint8_t byte : bytes) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
    }
    std::cout << std::dec << std::endl;
}

// Helper to print BoltError and associated reader/writer errors
void print_bolt_error_details(const std::string& context, boltprotocol::BoltError err, boltprotocol::PackStreamReader* reader = nullptr, boltprotocol::PackStreamWriter* writer = nullptr) {
    std::cerr << "Error " << context << ": " << static_cast<int>(err);
    if (reader && reader->has_error() && reader->get_error() != err) {  // Print only if different from main error
        std::cerr << " (Reader specific error: " << static_cast<int>(reader->get_error()) << ")";
    }
    if (writer && writer->has_error() && writer->get_error() != err) {  // Print only if different from main error
        std::cerr << " (Writer specific error: " << static_cast<int>(writer->get_error()) << ")";
    }
    std::cerr << std::endl;
}

int main() {
    using namespace boltprotocol;

    std::cout << "Bolt Protocol Client Example (No-Exception Mode)" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    std::vector<uint8_t> client_send_buffer_storage;  // Underlying storage for writers
    BoltError err = BoltError::SUCCESS;

    // --- (Optional: Handshake simulation - would use actual streams in real code) ---
    // Example:
    // {
    //     std::cout << "\nSimulating Handshake..." << std::endl;
    //     // Mock streams (in a real scenario, these would be socket streams)
    //     std::vector<uint8_t> mock_ostream_buffer;
    //     std::vector<uint8_t> mock_istream_buffer;
    //     // Simulate server response: Bolt 5.0 (00 00 05 00 in BE)
    //     mock_istream_buffer = {0x00, 0x00, 0x05, 0x00};
    //
    //     // Create stream wrappers if your perform_handshake needs them,
    //     // or adapt perform_handshake to work with raw byte vectors for simulation.
    //     // For simplicity, let's assume perform_handshake can be adapted or uses helper streams.
    //
    //     // This is a simplified stream concept for example purposes
    //     struct VectorStreambuf : public std::streambuf {
    //         VectorStreambuf(std::vector<uint8_t>& vec_out, std::vector<uint8_t>& vec_in) : vec_out_(vec_out), vec_in_(vec_in), in_pos_(0) {
    //             if (!vec_in_.empty()) {
    //                setg(reinterpret_cast<char*>(vec_in_.data()),
    //                     reinterpret_cast<char*>(vec_in_.data()),
    //                     reinterpret_cast<char*>(vec_in_.data() + vec_in_.size()));
    //             }
    //         }
    //         std::vector<uint8_t>& vec_out_;
    //         std::vector<uint8_t>& vec_in_;
    //         size_t in_pos_;
    //     protected:
    //         int overflow(int c) override {
    //             if (c != EOF) vec_out_.push_back(static_cast<uint8_t>(c));
    //             return c;
    //         }
    //         int underflow() override {
    //             if (in_pos_ < vec_in_.size()) return vec_in_[in_pos_];
    //             return EOF;
    //         }
    //         int uflow() override {
    //             if (in_pos_ < vec_in_.size()) return vec_in_[in_pos_++];
    //             return EOF;
    //         }
    //         std::streamsize xsputn(const char* s, std::streamsize n) override {
    //             vec_out_.insert(vec_out_.end(), s, s + n);
    //             return n;
    //         }
    //        std::streamsize xsgetn(char* s, std::streamsize n) override {
    //            std::streamsize num_to_read = std::min(n, static_cast<std::streamsize>(vec_in_.size() - in_pos_));
    //            if (num_to_read > 0) {
    //                std::memcpy(s, vec_in_.data() + in_pos_, num_to_read);
    //                in_pos_ += num_to_read;
    //            }
    //            return num_to_read;
    //        }
    //     };
    //     VectorStreambuf mock_streambuf(mock_ostream_buffer, mock_istream_buffer);
    //     std::iostream mock_socket_stream(&mock_streambuf);
    //
    //     std::vector<versions::Version> proposed_versions = {versions::V5_4, versions::V5_0, versions::V4_4};
    //     versions::Version negotiated_version;
    //
    //     err = perform_handshake(mock_socket_stream, mock_socket_stream, proposed_versions, negotiated_version);
    //     if (err != BoltError::SUCCESS) {
    //         print_bolt_error_details("performing handshake", err);
    //         return 1;
    //     }
    //     std::cout << "Handshake successful! Negotiated version: "
    //               << static_cast<int>(negotiated_version.major) << "."
    //               << static_cast<int>(negotiated_version.minor) << std::endl;
    //     print_bytes("Handshake request sent: ", mock_ostream_buffer);
    // }

    // --- 1. Simulate sending a HELLO message ---
    client_send_buffer_storage.clear();  // Ensure buffer is empty
    {                                    // Scope for writer
        PackStreamWriter writer(client_send_buffer_storage);
        HelloMessageParams hello_params;
        bool params_ok = true;
        try {
            hello_params.extra_auth_tokens.emplace("user_agent", Value(std::string("MyExampleCppClient/1.0")));
            hello_params.extra_auth_tokens.emplace("scheme", Value(std::string("basic")));
            hello_params.extra_auth_tokens.emplace("principal", Value(std::string("neo4j")));
            hello_params.extra_auth_tokens.emplace("credentials", Value(std::string("password")));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details("allocating HELLO params", BoltError::OUT_OF_MEMORY);
            params_ok = false;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception preparing HELLO params: " << e_std.what() << std::endl;
            print_bolt_error_details("preparing HELLO params (std::exception)", BoltError::UNKNOWN_ERROR);
            params_ok = false;
        }
        if (!params_ok) return 1;

        std::cout << "\nSerializing HELLO message..." << std::endl;
        err = serialize_hello_message(hello_params, writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details("serializing HELLO", err, nullptr, &writer);
            return 1;
        }
        // Assuming client_send_buffer_storage now contains the raw serialized message
        print_bytes("HELLO message bytes (raw): ", client_send_buffer_storage);
    }  // writer goes out of scope

    // Simulate these bytes would be chunked and sent over a socket.
    // For this example, bytes_sent_to_server IS the raw message.
    std::vector<uint8_t> bytes_sent_to_server = client_send_buffer_storage;
    // client_send_buffer_storage is implicitly cleared or reused by next writer if needed.

    // --- 2. Simulate server responding with SUCCESS to HELLO ---
    std::vector<uint8_t> bytes_received_from_server_storage;  // Server's response
    bytes_received_from_server_storage.clear();
    {  // Scope for server_ack_writer (simulating server side)
        PackStreamWriter server_ack_writer(bytes_received_from_server_storage);

        SuccessMessageParams success_params_from_server;
        std::shared_ptr<BoltMap> success_meta_map_sptr;
        std::shared_ptr<PackStreamStructure> pss_sptr;
        PackStreamStructure success_struct_server_obj;  // Temporary stack object
        bool server_prep_ok = true;

        try {
            success_params_from_server.metadata.emplace("connection_id", Value(std::string("bolt-12345")));
            success_params_from_server.metadata.emplace("server", Value(std::string("Neo4j/5.x.x")));

            success_struct_server_obj.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            success_meta_map_sptr = std::make_shared<BoltMap>();
            success_meta_map_sptr->pairs = std::move(success_params_from_server.metadata);  // metadata moved
            success_struct_server_obj.fields.emplace_back(Value(success_meta_map_sptr));    // sptr copied into Value

            pss_sptr = std::make_shared<PackStreamStructure>(std::move(success_struct_server_obj));

        } catch (const std::bad_alloc&) {
            print_bolt_error_details("server preparing SUCCESS (bad_alloc)", BoltError::OUT_OF_MEMORY);
            server_prep_ok = false;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception server preparing SUCCESS: " << e_std.what() << std::endl;
            print_bolt_error_details("server preparing SUCCESS (std::exception)", BoltError::UNKNOWN_ERROR);
            server_prep_ok = false;
        }
        if (!server_prep_ok) return 1;

        if (!pss_sptr) {  // Should be caught by bad_alloc if make_shared fails typically
            print_bolt_error_details("server preparing SUCCESS (null pss_sptr)", BoltError::OUT_OF_MEMORY);
            return 1;
        }

        err = server_ack_writer.write(Value(std::move(pss_sptr)));  // pss_sptr moved into Value
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details("server serializing SUCCESS for HELLO_ACK", err, nullptr, &server_ack_writer);
            return 1;
        }
        // bytes_received_from_server_storage now contains the raw serialized SUCCESS message
        print_bytes("Simulated SUCCESS (for HELLO_ACK) from server (raw): ", bytes_received_from_server_storage);
    }  // server_ack_writer goes out of scope

    // Client deserializes the SUCCESS response
    SuccessMessageParams received_success_params;
    {  // Scope for reader
        // Assume bytes_received_from_server_storage would be de-chunked by a ChunkedReader in a real scenario
        PackStreamReader client_hello_ack_reader(bytes_received_from_server_storage);
        std::cout << "\nClient deserializing SUCCESS message (for HELLO_ACK)..." << std::endl;
        err = deserialize_success_message(client_hello_ack_reader, received_success_params);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details("deserializing SUCCESS (for HELLO_ACK)", err, &client_hello_ack_reader);
            return 1;
        }
    }  // reader goes out of scope
    std::cout << "SUCCESS (for HELLO_ACK) deserialized successfully!" << std::endl;

    auto print_metadata_string_fn = [&](const std::string& key_to_find) {
        auto it = received_success_params.metadata.find(key_to_find);
        if (it != received_success_params.metadata.end()) {
            if (const auto* str_val = std::get_if<std::string>(&(it->second))) {
                std::cout << *str_val;
            } else {
                std::cout << "(metadata value for '" << key_to_find << "' not a string)";
            }
        } else {
            std::cout << "(metadata key '" << key_to_find << "' not found)";
        }
    };
    std::cout << "  Server version: ";
    print_metadata_string_fn("server");
    std::cout << std::endl;
    std::cout << "  Connection ID: ";
    print_metadata_string_fn("connection_id");
    std::cout << std::endl;

    // --- 3. Simulate sending a RUN message ---
    client_send_buffer_storage.clear();  // Clear buffer for the new message
    {                                    // Scope for run_writer
        PackStreamWriter run_writer(client_send_buffer_storage);
        RunMessageParams run_params;
        bool run_params_ok = true;
        try {
            run_params.cypher_query = "MATCH (n) RETURN n.name AS name LIMIT $limit";
            run_params.parameters.emplace("limit", Value(static_cast<int64_t>(10)));
        } catch (const std::bad_alloc&) {
            print_bolt_error_details("allocating RUN params", BoltError::OUT_OF_MEMORY);
            run_params_ok = false;
        } catch (const std::exception& e_std) {
            std::cerr << "Std exception preparing RUN params: " << e_std.what() << std::endl;
            print_bolt_error_details("preparing RUN params (std::exception)", BoltError::UNKNOWN_ERROR);
            run_params_ok = false;
        }
        if (!run_params_ok) return 1;

        std::cout << "\nSerializing RUN message..." << std::endl;
        err = serialize_run_message(run_params, run_writer);
        if (err != BoltError::SUCCESS) {
            print_bolt_error_details("serializing RUN", err, nullptr, &run_writer);
            return 1;
        }
        print_bytes("RUN message bytes (raw): ", client_send_buffer_storage);
    }  // run_writer goes out of scope

    // At this point, client_send_buffer_storage contains the raw RUN message.
    // In a real client, this would be passed to a ChunkedWriter.

    // ... Potentially simulate server response to RUN (SUCCESS with fields, RECORDs, SUCCESS summary) ...
    // ... and client deserializing those, with full error checking and try-catch for allocations ...

    std::cout << "\nClient example finished." << std::endl;
    return 0;
}