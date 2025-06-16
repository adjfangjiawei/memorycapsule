#include "client_example_tx_commit.h"

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "client_example_utils.h"

namespace ClientTransaction {

    boltprotocol::BoltError commit_transaction(ClientSession& session) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_message_bytes_storage;
        std::vector<uint8_t> raw_response_bytes_storage;

        std::cout << "\n--- Client Sending COMMIT ---" << std::endl;
        {
            PackStreamWriter ps_writer(raw_message_bytes_storage);
            session.last_error = serialize_commit_message(ps_writer);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("serializing COMMIT", session.last_error, nullptr, &ps_writer);
                return session.last_error;
            }
        }

        session.last_error = simulate_server_simple_success_response(session.server_to_client_stream, "COMMIT");
        if (session.last_error != BoltError::SUCCESS) return session.last_error;

        session.last_error = send_and_receive_raw_message_client(session.client_to_server_stream, session.server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "COMMIT");
        if (session.last_error != BoltError::SUCCESS) return session.last_error;

        if (raw_response_bytes_storage.empty()) {
            print_bolt_error_details_client("COMMIT resp empty", BoltError::DESERIALIZATION_ERROR);
            session.last_error = BoltError::DESERIALIZATION_ERROR;
            return session.last_error;
        }
        SuccessMessageParams commit_success_params;
        {
            PackStreamReader r(raw_response_bytes_storage);
            session.last_error = deserialize_success_message(r, commit_success_params);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("deser COMMIT SUCCESS", session.last_error, &r);
                return session.last_error;
            }
        }
        std::cout << "Client: COMMIT SUCCESS deserialized." << std::endl;
        // Check for bookmark from commit_success_params.metadata if needed
        auto it_bookmark = commit_success_params.metadata.find("bookmark");
        if (it_bookmark != commit_success_params.metadata.end()) {
            if (const auto* str_val = std::get_if<std::string>(&(it_bookmark->second))) {
                std::cout << "Client: Received bookmark from COMMIT: " << *str_val << std::endl;
            }
        }
        return BoltError::SUCCESS;
    }

    // Implement rollback_transaction here if needed in the future
    /*
    boltprotocol::BoltError rollback_transaction(ClientSession& session) {
        // ... similar to commit_transaction but sends ROLLBACK and expects SUCCESS
        return BoltError::SUCCESS;
    }
    */

}  // namespace ClientTransaction