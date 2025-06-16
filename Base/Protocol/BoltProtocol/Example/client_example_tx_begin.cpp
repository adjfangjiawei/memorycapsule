#include "client_example_tx_begin.h"

#include "boltprotocol/message_serialization.h"  // For serialize_begin_message
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "client_example_utils.h"  // For simulate_server_simple_success_response, send_and_receive_raw_message_client

namespace ClientTransaction {

    boltprotocol::BoltError begin_transaction(ClientSession& session) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_message_bytes_storage;
        std::vector<uint8_t> raw_response_bytes_storage;

        std::cout << "\n--- Client Sending BEGIN ---" << std::endl;
        {
            PackStreamWriter ps_writer(raw_message_bytes_storage);
            BeginMessageParams begin_params;
            // Populate begin_params with specific fields if needed, e.g., based on session.negotiated_version
            // begin_params.tx_timeout = 5000; // Example
            session.last_error = serialize_begin_message(begin_params, ps_writer, session.negotiated_version);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("serializing BEGIN", session.last_error, nullptr, &ps_writer);
                return session.last_error;
            }
        }

        session.last_error = simulate_server_simple_success_response(session.server_to_client_stream, "BEGIN");
        if (session.last_error != BoltError::SUCCESS) return session.last_error;

        session.last_error = send_and_receive_raw_message_client(session.client_to_server_stream, session.server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "BEGIN");
        if (session.last_error != BoltError::SUCCESS) return session.last_error;

        if (raw_response_bytes_storage.empty()) {
            print_bolt_error_details_client("BEGIN resp empty", BoltError::DESERIALIZATION_ERROR);
            session.last_error = BoltError::DESERIALIZATION_ERROR;
            return session.last_error;
        }
        SuccessMessageParams begin_success_params;
        {
            PackStreamReader r(raw_response_bytes_storage);
            session.last_error = deserialize_success_message(r, begin_success_params);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("deser BEGIN SUCCESS", session.last_error, &r);
                return session.last_error;
            }
        }
        std::cout << "Client: BEGIN SUCCESS deserialized." << std::endl;
        return BoltError::SUCCESS;
    }

}  // namespace ClientTransaction