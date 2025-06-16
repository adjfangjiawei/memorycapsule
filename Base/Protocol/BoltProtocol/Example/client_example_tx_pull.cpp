#include "client_example_tx_pull.h"

#include "boltprotocol/chunking.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "client_example_utils.h"

namespace ClientTransaction {

    // Helper to simulate server sending a RECORD message
    boltprotocol::BoltError simulate_server_record_response(std::stringstream& server_pipe, const std::vector<boltprotocol::Value>& record_fields) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_server_response_bytes;
        BoltError err;
        {
            PackStreamWriter srv_ps_writer(raw_server_response_bytes);
            RecordMessageParams rec_p;
            try {
                rec_p.fields = record_fields;  // Copy
            } catch (...) {
                return BoltError::OUT_OF_MEMORY; /* Simplified */
            }

            PackStreamStructure pss_rec_obj;
            pss_rec_obj.tag = static_cast<uint8_t>(MessageTag::RECORD);
            std::shared_ptr<BoltList> list_sptr;
            try {
                list_sptr = std::make_shared<BoltList>();
                list_sptr->elements = std::move(rec_p.fields);
                pss_rec_obj.fields.emplace_back(Value(list_sptr));
            } catch (...) {
                return BoltError::OUT_OF_MEMORY; /* Simplified */
            }

            std::shared_ptr<PackStreamStructure> pss_rec_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_rec_obj));
            if (!pss_rec_to_write_sptr) {
                return BoltError::OUT_OF_MEMORY;
            }

            err = srv_ps_writer.write(Value(pss_rec_to_write_sptr));
            if (err != BoltError::SUCCESS) {
                print_bolt_error_details_client("Sim Srv: serializing RECORD", err, nullptr, &srv_ps_writer);
                return err;
            }
        }
        server_pipe.clear();
        server_pipe.str("");
        {
            ChunkedWriter srv_c_writer(server_pipe);
            err = srv_c_writer.write_message(raw_server_response_bytes);
            if (err != BoltError::SUCCESS) {
                print_bolt_error_details_client("Sim Srv: chunking RECORD", err, nullptr, nullptr, nullptr, &srv_c_writer);
                return err;
            }
        }
        std::cout << "Server (Simulated): Sent RECORD response." << std::endl;
        return BoltError::SUCCESS;
    }

    boltprotocol::BoltError pull_all_results_in_transaction(ClientSession& session, int64_t qid, std::vector<boltprotocol::RecordMessageParams>& out_records) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_pull_message_bytes;
        std::vector<uint8_t> raw_response_bytes_storage;
        out_records.clear();

        std::cout << "\n--- Client Sending PULL (in transaction) for qid: " << qid << " ---" << std::endl;
        {
            PackStreamWriter ps_writer(raw_pull_message_bytes);
            PullMessageParams pull_params;
            pull_params.n = -1;  // PULL ALL
            if (qid != -1) {     // qid is mandatory for PULL in explicit transaction
                pull_params.qid = qid;
            } else {
                // This is an issue if qid is expected.
                print_bolt_error_details_client("PULL: qid is -1, which is invalid for explicit TX PULL", BoltError::INVALID_ARGUMENT);
                session.last_error = BoltError::INVALID_ARGUMENT;
                return session.last_error;
            }

            session.last_error = serialize_pull_message(pull_params, ps_writer);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("serializing PULL", session.last_error, nullptr, &ps_writer);
                return session.last_error;
            }
        }

        bool first_pull_interaction = true;
        bool has_more_server_says = true;

        while (has_more_server_says) {
            // --- Simulate Server Response for PULL ---
            // This simulation sends one record, then a SUCCESS summary.
            // A real server might send many records before a SUCCESS with has_more:true,
            // or end with has_more:false.
            if (first_pull_interaction) {
                // Simulate server sending one RECORD
                std::vector<Value> record_data;
                try {
                    record_data.emplace_back(Value(static_cast<int64_t>(12345)));
                }  // Dummy ID
                catch (...) {
                    session.last_error = BoltError::OUT_OF_MEMORY;
                    return session.last_error;
                }
                session.last_error = simulate_server_record_response(session.server_to_client_stream, record_data);
                if (session.last_error != BoltError::SUCCESS) return session.last_error;
            } else {
                // After the first (and only in this sim) record, server sends SUCCESS summary
                // Simulate has_more:false to end the loop.
                // In a real scenario, server might send SUCCESS with has_more:true if there are more batches.
                session.last_error = simulate_server_simple_success_response(session.server_to_client_stream, "PULL summary (final)", qid);
                // To make it more realistic, the success response should include "has_more":false
                // We'd need to modify simulate_server_simple_success_response or use a more specific one.
                // For now, our client loop will break when it gets a SUCCESS.
                if (session.last_error != BoltError::SUCCESS) return session.last_error;
            }

            // --- Client Sends PULL (first time) and Receives Response ---
            std::vector<uint8_t> message_to_send_this_iteration;
            if (first_pull_interaction) {
                message_to_send_this_iteration = raw_pull_message_bytes;
            }

            session.last_error = send_and_receive_raw_message_client(session.client_to_server_stream, session.server_to_client_stream, message_to_send_this_iteration, raw_response_bytes_storage, first_pull_interaction ? "PULL (for RECORD)" : "PULL (for summary)");
            if (session.last_error != BoltError::SUCCESS) return session.last_error;
            first_pull_interaction = false;

            if (raw_response_bytes_storage.empty()) {
                print_bolt_error_details_client("PULL response empty", BoltError::DESERIALIZATION_ERROR);
                session.last_error = BoltError::DESERIALIZATION_ERROR;
                return session.last_error;
            }

            // --- Client Deserializes Response ---
            Value peek_value;
            PackStreamStructure received_pss;  // To store the actual structure
            {
                PackStreamReader peek_reader(raw_response_bytes_storage);
                // We need to get the actual PSS, not just the shared_ptr from Value for tag checking.
                BoltError temp_err = deserialize_message_structure_prelude(peek_reader, MessageTag::HELLO, 0, 1, received_pss);  // Tag doesn't matter for just getting fields
                if (peek_reader.has_error() && temp_err == BoltError::SUCCESS) temp_err = peek_reader.get_error();               // ensure err state is from reader if prelude was ok

                if (temp_err != BoltError::SUCCESS && temp_err != BoltError::INVALID_MESSAGE_FORMAT) {  // Allow tag mismatch
                    print_bolt_error_details_client("Peeking PULL response structure", temp_err, &peek_reader);
                    session.last_error = temp_err;
                    return session.last_error;
                }
                // If it was INVALID_MESSAGE_FORMAT due to tag, that's fine, we check tag below.
                // If it was another error (e.g. not a PSS at all), that's a problem.
                if (!peek_reader.has_error() && received_pss.fields.empty() && received_pss.tag == 0) {  // deserialize_message_structure_prelude failed to get a PSS
                    print_bolt_error_details_client("Peeking PULL response: not a valid PSS", BoltError::DESERIALIZATION_ERROR, &peek_reader);
                    session.last_error = BoltError::DESERIALIZATION_ERROR;
                    return session.last_error;
                }
            }

            if (static_cast<MessageTag>(received_pss.tag) == MessageTag::RECORD) {
                RecordMessageParams rec_params;
                PackStreamReader record_reader(raw_response_bytes_storage);
                session.last_error = deserialize_record_message(record_reader, rec_params);
                if (session.last_error != BoltError::SUCCESS) {
                    print_bolt_error_details_client("Deserializing RECORD from PULL", session.last_error, &record_reader);
                    return session.last_error;
                }
                out_records.push_back(std::move(rec_params));
                std::cout << "Client: RECORD deserialized from PULL." << std::endl;
                // Check for "has_more" in record metadata if present (uncommon, usually in SUCCESS)
                // For this simulation, we assume has_more_server_says is true until SUCCESS says otherwise
            } else if (static_cast<MessageTag>(received_pss.tag) == MessageTag::SUCCESS) {
                SuccessMessageParams summary_params;
                PackStreamReader summary_reader(raw_response_bytes_storage);
                session.last_error = deserialize_success_message(summary_reader, summary_params);
                if (session.last_error != BoltError::SUCCESS) {
                    print_bolt_error_details_client("Deserializing SUCCESS summary from PULL", session.last_error, &summary_reader);
                    return session.last_error;
                }
                std::cout << "Client: PULL summary SUCCESS deserialized." << std::endl;

                // Check for "has_more" in the SUCCESS metadata
                auto it_has_more = summary_params.metadata.find("has_more");
                if (it_has_more != summary_params.metadata.end() && std::holds_alternative<bool>(it_has_more->second)) {
                    has_more_server_says = std::get<bool>(it_has_more->second);
                    std::cout << "Client: PULL summary has_more=" << (has_more_server_says ? "true" : "false") << std::endl;
                } else {
                    has_more_server_says = false;  // If not present, assume no more (Bolt 3 behavior)
                    std::cout << "Client: PULL summary 'has_more' not found or not bool, assuming false." << std::endl;
                }
            } else {
                print_bolt_error_details_client("PULL response unexpected PSS tag: " + std::to_string(received_pss.tag), BoltError::INVALID_MESSAGE_FORMAT);
                session.last_error = BoltError::INVALID_MESSAGE_FORMAT;
                return session.last_error;
            }
        }
        return BoltError::SUCCESS;
    }

}  // namespace ClientTransaction