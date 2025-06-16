#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "client_example_session.h"  // For ClientSession struct
#include "client_example_utils.h"    // For print_bolt_error_details_client if main directly reports an error

// Include new transaction step headers
#include "boltprotocol/message_defs.h"  // For BoltError, Value
#include "client_example_tx_begin.h"
#include "client_example_tx_commit.h"
#include "client_example_tx_pull.h"
#include "client_example_tx_run.h"

int main() {
    using namespace boltprotocol;

    std::cout << "Bolt Protocol Client Example (No-Exception, Refactored TX)" << std::endl;
    std::cout << "-----------------------------------------------------------" << std::endl;

    ClientSession session;  // Manages streams, negotiated_version, last_error

    // --- 0. Perform Handshake ---
    std::cout << "\n--- Performing Handshake ---" << std::endl;
    if (session.perform_handshake_sequence() != BoltError::SUCCESS) {
        return 1;
    }

    // --- 1. Client Sends HELLO Message ---
    std::cout << "\n--- Client Sending HELLO ---" << std::endl;
    if (session.send_hello_sequence() != BoltError::SUCCESS) {
        return 1;
    }

    // --- Transaction Block ---
    std::cout << "\n--- Starting Transaction Block ---" << std::endl;
    if (ClientTransaction::begin_transaction(session) != BoltError::SUCCESS) {
        session.send_goodbye_sequence();
        return 1;
    }

    int64_t query_id = -1;
    std::string test_query = "CREATE (a:Person {name: 'Alice'}) RETURN id(a)";
    std::map<std::string, Value> test_params;

    // std::cout << "\n--- Client Sending RUN (in transaction) ---" << std::endl; // Moved into function
    if (ClientTransaction::run_query_in_transaction(session, test_query, test_params, query_id) != BoltError::SUCCESS) {
        // Consider ROLLBACK here
        session.send_goodbye_sequence();
        return 1;
    }

    if (query_id != -1) {
        std::vector<RecordMessageParams> records;
        // std::cout << "\n--- Client Sending PULL (in transaction) for qid: " << query_id << " ---" << std::endl; // Moved
        if (ClientTransaction::pull_all_results_in_transaction(session, query_id, records) != BoltError::SUCCESS) {
            session.send_goodbye_sequence();
            return 1;
        }
        std::cout << "Client: PULL sequence successful. Received " << records.size() << " records." << std::endl;
        for (const auto& record_param : records) {
            std::cout << "  Record: ";
            for (const auto& field_value : record_param.fields) {
                if (std::holds_alternative<int64_t>(field_value)) {
                    std::cout << std::get<int64_t>(field_value) << " ";
                } else if (std::holds_alternative<std::string>(field_value)) {
                    std::cout << "\"" << std::get<std::string>(field_value) << "\" ";
                } else {
                    std::cout << "[type_idx:" << field_value.index() << "] ";
                }
            }
            std::cout << std::endl;
        }
    } else {
        std::cout << "Client: No valid qid from RUN, or qid indicates no results to pull. Skipping PULL." << std::endl;
    }

    // std::cout << "\n--- Client Sending COMMIT ---" << std::endl; // Moved
    if (ClientTransaction::commit_transaction(session) != BoltError::SUCCESS) {
        session.send_goodbye_sequence();
        return 1;
    }
    std::cout << "--- Transaction Block Finished ---" << std::endl;

    // --- 5. Client Sends GOODBYE ---
    std::cout << "\n--- Client Sending GOODBYE ---" << std::endl;
    if (session.send_goodbye_sequence() != BoltError::SUCCESS) {
        return 1;
    }

    std::cout << "\nClient example finished successfully." << std::endl;
    return 0;
}