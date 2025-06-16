#include "client_example_tx_run.h"

#include "boltprotocol/chunking.h"  // For ChunkedWriter if server sim uses it directly
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "client_example_utils.h"

namespace ClientTransaction {

    // Helper to simulate server RUN response (SUCCESS with fields and qid)
    // This could also live in client_example_utils.cpp if it's more general
    boltprotocol::BoltError simulate_server_run_response_fields(std::stringstream& server_pipe, const std::vector<std::string>& field_names, int64_t qid) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_server_response_bytes;
        BoltError err;
        {
            PackStreamWriter srv_ps_writer(raw_server_response_bytes);
            SuccessMessageParams fields_s_p;
            bool prep_ok = true;
            try {
                auto fields_list_sptr = std::make_shared<BoltList>();
                for (const auto& field_name : field_names) {
                    fields_list_sptr->elements.emplace_back(Value(field_name));
                }
                fields_s_p.metadata.emplace("fields", Value(fields_list_sptr));
                if (qid != -1) {
                    fields_s_p.metadata.emplace("qid", Value(qid));
                }
            } catch (const std::bad_alloc&) {
                print_bolt_error_details_client("Sim Srv: RUN SUCCESS fields alloc", BoltError::OUT_OF_MEMORY);
                prep_ok = false;
                return BoltError::OUT_OF_MEMORY;
            } catch (const std::exception& e) {
                std::cerr << "StdExc Sim Srv: RUN SUCCESS fields: " << e.what() << std::endl;
                print_bolt_error_details_client("Sim Srv: RUN SUCCESS fields stdexc", BoltError::UNKNOWN_ERROR);
                prep_ok = false;
                return BoltError::UNKNOWN_ERROR;
            }
            if (!prep_ok) return BoltError::UNKNOWN_ERROR;

            PackStreamStructure pss_obj;
            pss_obj.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            std::shared_ptr<BoltMap> meta_map_sptr;
            try {
                meta_map_sptr = std::make_shared<BoltMap>();
                meta_map_sptr->pairs = std::move(fields_s_p.metadata);
                pss_obj.fields.emplace_back(Value(meta_map_sptr));
            } catch (...) {
                return BoltError::UNKNOWN_ERROR; /* Simplified error */
            }

            std::shared_ptr<PackStreamStructure> pss_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_obj));
            if (!pss_to_write_sptr) {
                return BoltError::OUT_OF_MEMORY;
            }

            err = srv_ps_writer.write(Value(pss_to_write_sptr));
            if (err != BoltError::SUCCESS) {
                print_bolt_error_details_client("Sim Srv: serializing RUN SUCCESS fields", err, nullptr, &srv_ps_writer);
                return err;
            }
        }
        // Prime server_pipe with the response
        server_pipe.clear();
        server_pipe.str("");
        {
            ChunkedWriter srv_c_writer(server_pipe);
            err = srv_c_writer.write_message(raw_server_response_bytes);
            if (err != BoltError::SUCCESS) {
                print_bolt_error_details_client("Sim Srv: chunking RUN SUCCESS fields", err, nullptr, nullptr, nullptr, &srv_c_writer);
                return err;
            }
        }
        std::cout << "Server (Simulated): Sent RUN SUCCESS (fields, qid=" << qid << ") response." << std::endl;
        return BoltError::SUCCESS;
    }

    boltprotocol::BoltError run_query_in_transaction(ClientSession& session, const std::string& query, const std::map<std::string, boltprotocol::Value>& params, int64_t& out_qid) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_message_bytes_storage;
        std::vector<uint8_t> raw_response_bytes_storage;
        out_qid = -1;

        std::cout << "\n--- Client Sending RUN (in transaction) ---" << std::endl;
        {
            PackStreamWriter ps_writer(raw_message_bytes_storage);
            RunMessageParams run_params;
            bool prep_ok = true;
            try {
                run_params.cypher_query = query;
                run_params.parameters = params;
                // Example of setting specific extra fields for RUN
                // run_params.db = "mydb"; // If appropriate for the version
            } catch (const std::bad_alloc&) {
                print_bolt_error_details_client("alloc RUN_IN_TX params", BoltError::OUT_OF_MEMORY);
                prep_ok = false;
                session.last_error = BoltError::OUT_OF_MEMORY;
            } catch (const std::exception& e) {
                std::cerr << "StdExc RUN_IN_TX params: " << e.what() << std::endl;
                print_bolt_error_details_client("prep RUN_IN_TX params", BoltError::UNKNOWN_ERROR);
                prep_ok = false;
                session.last_error = BoltError::UNKNOWN_ERROR;
            }
            if (!prep_ok) return session.last_error;

            session.last_error = serialize_run_message(run_params, ps_writer, session.negotiated_version);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("serializing RUN_IN_TX", session.last_error, nullptr, &ps_writer);
                return session.last_error;
            }
        }

        int64_t simulated_qid = 2;                              // Server would generate this
        std::vector<std::string> simulated_fields = {"id(a)"};  // Match "CREATE (a:Person {name: 'Alice'}) RETURN id(a)"
        session.last_error = simulate_server_run_response_fields(session.server_to_client_stream, simulated_fields, simulated_qid);
        if (session.last_error != BoltError::SUCCESS) return session.last_error;

        session.last_error = send_and_receive_raw_message_client(session.client_to_server_stream, session.server_to_client_stream, raw_message_bytes_storage, raw_response_bytes_storage, "RUN_IN_TX");
        if (session.last_error != BoltError::SUCCESS) return session.last_error;

        if (raw_response_bytes_storage.empty()) {
            print_bolt_error_details_client("RUN_IN_TX resp empty", BoltError::DESERIALIZATION_ERROR);
            session.last_error = BoltError::DESERIALIZATION_ERROR;
            return session.last_error;
        }
        SuccessMessageParams run_in_tx_success_params;
        {
            PackStreamReader r(raw_response_bytes_storage);
            session.last_error = deserialize_success_message(r, run_in_tx_success_params);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("deser RUN_IN_TX SUCCESS", session.last_error, &r);
                return session.last_error;
            }
        }

        auto it_qid = run_in_tx_success_params.metadata.find("qid");
        if (it_qid != run_in_tx_success_params.metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
            out_qid = std::get<int64_t>(it_qid->second);
        } else {
            std::cout << "Client: Warning - qid not found or not int64 in RUN SUCCESS metadata for TX." << std::endl;
            // For auto-commit RUN, qid might not be present if no results are expected or version is old.
            // For explicit TX RUN, qid is usually expected.
        }
        std::cout << "Client: RUN_IN_TX SUCCESS (fields) deserialized. qid: " << out_qid << std::endl;
        return BoltError::SUCCESS;
    }

}  // namespace ClientTransaction