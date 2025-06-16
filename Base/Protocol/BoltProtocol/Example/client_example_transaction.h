#ifndef CLIENT_EXAMPLE_TRANSACTION_H
#define CLIENT_EXAMPLE_TRANSACTION_H

#include <map>  // For query parameters
#include <sstream>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "client_example_utils.h"  // For helpers

// Forward declaration
struct ClientSession;

namespace ClientTransaction {

    boltprotocol::BoltError begin_transaction(ClientSession& session);

    // Returns qid through out_qid parameter
    boltprotocol::BoltError run_query_in_transaction(ClientSession& session, const std::string& query, const std::map<std::string, boltprotocol::Value>& params, int64_t& out_qid);

    // Takes qid as input, fetches all records and the summary
    boltprotocol::BoltError pull_all_results_in_transaction(ClientSession& session, int64_t qid, std::vector<boltprotocol::RecordMessageParams>& out_records);

    boltprotocol::BoltError commit_transaction(ClientSession& session);

    // boltprotocol::BoltError rollback_transaction(ClientSession& session); // Example for future

}  // namespace ClientTransaction

#endif  // CLIENT_EXAMPLE_TRANSACTION_H