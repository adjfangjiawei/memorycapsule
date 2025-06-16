#include "client_example_transaction.h"

#include "client_example_session.h"  // For ClientSession definition

namespace ClientTransaction {

    boltprotocol::BoltError begin_transaction(ClientSession& session) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_message_bytes_storage;
        std::vector<uint8_t> raw_response_bytes_storage;

        {
            PackStreamWriter ps_writer(raw_message_bytes_storage);
            BeginMessageParams begin_params;  // Empty 'extra' map by default
            session.last_error = serialize_begin_message(begin_params, ps_writer);
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

    boltprotocol::BoltError run_query_in_transaction(ClientSession& session, const std::string& query, const std::map<std::string, boltprotocol::Value>& params, int64_t& out_qid) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_message_bytes_storage;
        std::vector<uint8_t> raw_response_bytes_storage;
        out_qid = -1;  // Default to invalid qid

        // Serialize RUN
        {
            PackStreamWriter ps_writer(raw_message_bytes_storage);
            RunMessageParams run_params;
            bool prep_ok = true;
            try {
                run_params.cypher_query = query;
                run_params.parameters = params;
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

            session.last_error = serialize_run_message(run_params, ps_writer);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("serializing RUN_IN_TX", session.last_error, nullptr, &ps_writer);
                return session.last_error;
            }
        }

        // Simulate Server Response to RUN (SUCCESS {fields, qid})
        std::vector<uint8_t> raw_server_run_fields_response;
        int64_t simulated_qid = 2;  // Example qid, server would generate this
        {
            PackStreamWriter srv_ps_writer(raw_server_run_fields_response);
            SuccessMessageParams fields_s_p;
            bool prep_ok = true;
            try {
                auto fields_list = std::make_shared<BoltList>();
                fields_list->elements.emplace_back(Value(std::string("id(a)")));  // Match query "RETURN id(a)"
                fields_s_p.metadata.emplace("fields", Value(fields_list));
                fields_s_p.metadata.emplace("qid", Value(simulated_qid));
            } catch (const std::bad_alloc&) {
                print_bolt_error_details_client("Sim Srv: RUN_IN_TX SUCCESS fields alloc", BoltError::OUT_OF_MEMORY);
                prep_ok = false;
                session.last_error = BoltError::OUT_OF_MEMORY;
            } catch (const std::exception& e) {
                std::cerr << "StdExc Sim Srv: RUN_IN_TX SUCCESS fields: " << e.what() << std::endl;
                print_bolt_error_details_client("Sim Srv: RUN_IN_TX SUCCESS fields stdexc", BoltError::UNKNOWN_ERROR);
                prep_ok = false;
                session.last_error = BoltError::UNKNOWN_ERROR;
            }
            if (!prep_ok) return session.last_error;

            PackStreamStructure pss_obj;
            pss_obj.tag = static_cast<uint8_t>(MessageTag::SUCCESS);
            std::shared_ptr<BoltMap> meta_map_sptr;
            try {
                meta_map_sptr = std::make_shared<BoltMap>();
                meta_map_sptr->pairs = std::move(fields_s_p.metadata);
                pss_obj.fields.emplace_back(Value(meta_map_sptr));
            } catch (...) {
                session.last_error = BoltError::UNKNOWN_ERROR;
                return session.last_error;
            }

            std::shared_ptr<PackStreamStructure> pss_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_obj));
            if (!pss_to_write_sptr) {
                session.last_error = BoltError::OUT_OF_MEMORY;
                return session.last_error;
            }

            session.last_error = srv_ps_writer.write(Value(pss_to_write_sptr));
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("Sim Srv: serializing RUN_IN_TX SUCCESS fields", session.last_error, nullptr, &srv_ps_writer);
                return session.last_error;
            }
        }
        // Prime server_to_client_stream with the response
        session.server_to_client_stream.clear();
        session.server_to_client_stream.str("");
        {
            ChunkedWriter srv_c_writer(session.server_to_client_stream);
            session.last_error = srv_c_writer.write_message(raw_server_run_fields_response);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("Sim Srv: chunking RUN_IN_TX SUCCESS fields", session.last_error, nullptr, nullptr, nullptr, &srv_c_writer);
                return session.last_error;
            }
        }

        // Client sends RUN and gets response
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
        // Extract qid from response
        auto it_qid = run_in_tx_success_params.metadata.find("qid");
        if (it_qid != run_in_tx_success_params.metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
            out_qid = std::get<int64_t>(it_qid->second);
        } else {
            std::cout << "Client: Warning - qid not found or not int64 in RUN SUCCESS metadata." << std::endl;
        }
        std::cout << "Client: RUN_IN_TX SUCCESS (fields) deserialized. qid: " << out_qid << std::endl;
        return BoltError::SUCCESS;
    }

    boltprotocol::BoltError pull_all_results_in_transaction(ClientSession& session,
                                                            int64_t qid,  // qid from RUN
                                                            std::vector<boltprotocol::RecordMessageParams>& out_records) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_pull_message_bytes;
        std::vector<uint8_t> raw_response_bytes_storage;
        out_records.clear();

        // Serialize PULL message
        {
            PackStreamWriter ps_writer(raw_pull_message_bytes);
            PullMessageParams pull_params;
            bool prep_ok = true;
            try {
                pull_params.n = static_cast<int64_t>(-1);  // PULL ALL
                if (qid != -1) {
                    pull_params.qid = qid;
                }
            } catch (const std::bad_alloc&) {
                print_bolt_error_details_client("alloc PULL params", BoltError::OUT_OF_MEMORY);
                prep_ok = false;
                session.last_error = BoltError::OUT_OF_MEMORY;
            } catch (const std::exception& e) {
                std::cerr << "StdExc PULL params: " << e.what() << std::endl;
                print_bolt_error_details_client("prep PULL params", BoltError::UNKNOWN_ERROR);
                prep_ok = false;
                session.last_error = BoltError::UNKNOWN_ERROR;
            }
            if (!prep_ok) return session.last_error;

            session.last_error = serialize_pull_message(pull_params, ps_writer);
            if (session.last_error != BoltError::SUCCESS) {
                print_bolt_error_details_client("serializing PULL", session.last_error, nullptr, &ps_writer);
                return session.last_error;
            }
        }

        bool first_pull_interaction = true;
        bool has_more_records_pending = true;

        while (has_more_records_pending) {
            // Simulate Server Responses for PULL
            // In this simplified example, server sends one RECORD then a SUCCESS summary.
            // A real server would send multiple RECORDs if available.
            if (first_pull_interaction) {  // Assume this is the first server response for this PULL
                                           // Simulate one RECORD message from server
                std::vector<uint8_t> raw_server_record_response;
                {
                    PackStreamWriter srv_ps_writer(raw_server_record_response);
                    RecordMessageParams rec_p;
                    try {
                        rec_p.fields.emplace_back(Value(static_cast<int64_t>(12345)));
                    }  // Dummy ID
                    catch (...) {
                        session.last_error = BoltError::OUT_OF_MEMORY;
                        return session.last_error;
                    }

                    PackStreamStructure pss_rec_obj;
                    pss_rec_obj.tag = static_cast<uint8_t>(MessageTag::RECORD);
                    std::shared_ptr<BoltList> list_sptr;
                    try {
                        list_sptr = std::make_shared<BoltList>();
                        list_sptr->elements = std::move(rec_p.fields);
                        pss_rec_obj.fields.emplace_back(Value(list_sptr));
                    } catch (...) {
                        session.last_error = BoltError::OUT_OF_MEMORY;
                        return session.last_error;
                    }

                    std::shared_ptr<PackStreamStructure> pss_rec_to_write_sptr = std::make_shared<PackStreamStructure>(std::move(pss_rec_obj));
                    if (!pss_rec_to_write_sptr) {
                        session.last_error = BoltError::OUT_OF_MEMORY;
                        return session.last_error;
                    }

                    session.last_error = srv_ps_writer.write(Value(pss_rec_to_write_sptr));
                    if (session.last_error != BoltError::SUCCESS) {
                        print_bolt_error_details_client("Sim Srv: serializing RECORD for PULL", session.last_error, nullptr, &srv_ps_writer);
                        return session.last_error;
                    }
                }
                session.server_to_client_stream.clear();
                session.server_to_client_stream.str("");
                {
                    ChunkedWriter srv_c_writer(session.server_to_client_stream);
                    session.last_error = srv_c_writer.write_message(raw_server_record_response);
                    if (session.last_error != BoltError::SUCCESS) {
                        print_bolt_error_details_client("Sim Srv: chunking RECORD for PULL", session.last_error, nullptr, nullptr, nullptr, &srv_c_writer);
                        return session.last_error;
                    }
                }
            } else {  // Subsequent interactions: server must send SUCCESS summary (as we only simulated one record)
                session.last_error = simulate_server_simple_success_response(session.server_to_client_stream, "PULL summary", qid);
                if (session.last_error != BoltError::SUCCESS) return session.last_error;
            }

            // Client sends PULL message (only first time) and receives response
            std::vector<uint8_t> message_to_send_this_iteration;
            if (first_pull_interaction) {
                message_to_send_this_iteration = raw_pull_message_bytes;
            }  // Else, empty message_to_send_this_iteration (just receiving)

            session.last_error = send_and_receive_raw_message_client(session.client_to_server_stream, session.server_to_client_stream, message_to_send_this_iteration, raw_response_bytes_storage, first_pull_interaction ? "PULL (for RECORD)" : "PULL (for summary SUCCESS)");
            if (session.last_error != BoltError::SUCCESS) return session.last_error;
            first_pull_interaction = false;  // Subsequent interactions are just receiving for this PULL

            if (raw_response_bytes_storage.empty()) {
                print_bolt_error_details_client("PULL response empty", BoltError::DESERIALIZATION_ERROR);
                session.last_error = BoltError::DESERIALIZATION_ERROR;
                return session.last_error;
            }

            // Peek message tag to decide if it's RECORD or SUCCESS
            Value peek_value;
            {  // Temporary reader to peek
                PackStreamReader peek_reader(raw_response_bytes_storage);
                session.last_error = peek_reader.read(peek_value);
                if (session.last_error != BoltError::SUCCESS) {
                    print_bolt_error_details_client("Peeking PULL response", session.last_error, &peek_reader);
                    return session.last_error;
                }
            }

            if (std::holds_alternative<std::shared_ptr<PackStreamStructure>>(peek_value)) {
                auto pss = std::get<std::shared_ptr<PackStreamStructure>>(peek_value);
                if (pss && pss->tag == static_cast<uint8_t>(MessageTag::RECORD)) {
                    RecordMessageParams rec_params;
                    PackStreamReader record_reader(raw_response_bytes_storage);  // Fresh reader for full message
                    session.last_error = deserialize_record_message(record_reader, rec_params);
                    if (session.last_error != BoltError::SUCCESS) {
                        print_bolt_error_details_client("Deserializing RECORD from PULL", session.last_error, &record_reader);
                        return session.last_error;
                    }
                    out_records.push_back(std::move(rec_params));
                    std::cout << "Client: RECORD deserialized from PULL." << std::endl;
                    // has_more_records_pending remains true, loop again (server will send SUCCESS next in this sim)
                } else if (pss && pss->tag == static_cast<uint8_t>(MessageTag::SUCCESS)) {
                    SuccessMessageParams summary_params;
                    PackStreamReader summary_reader(raw_response_bytes_storage);  // Fresh reader
                    session.last_error = deserialize_success_message(summary_reader, summary_params);
                    if (session.last_error != BoltError::SUCCESS) {
                        print_bolt_error_details_client("Deserializing SUCCESS summary from PULL", session.last_error, &summary_reader);
                        return session.last_error;
                    }
                    std::cout << "Client: PULL summary SUCCESS deserialized." << std::endl;
                    // Check if has_more is true in metadata, for real servers. Our sim ends here.
                    has_more_records_pending = false;  // Stop loop
                } else {
                    print_bolt_error_details_client("PULL response unexpected PSS tag", BoltError::INVALID_MESSAGE_FORMAT);
                    session.last_error = BoltError::INVALID_MESSAGE_FORMAT;
                    return session.last_error;
                }
            } else {
                print_bolt_error_details_client("PULL response not a PSS", BoltError::INVALID_MESSAGE_FORMAT);
                session.last_error = BoltError::INVALID_MESSAGE_FORMAT;
                return session.last_error;
            }
        }
        return BoltError::SUCCESS;
    }

    boltprotocol::BoltError commit_transaction(ClientSession& session) {
        using namespace boltprotocol;
        std::vector<uint8_t> raw_message_bytes_storage;
        std::vector<uint8_t> raw_response_bytes_storage;

        {
            PackStreamWriter ps_writer(raw_message_bytes_storage);
            // COMMIT has no parameters in its params struct, just an empty map as the field in PSS
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
        return BoltError::SUCCESS;
    }

}  // namespace ClientTransaction