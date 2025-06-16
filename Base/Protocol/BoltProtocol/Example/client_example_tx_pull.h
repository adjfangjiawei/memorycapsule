#ifndef CLIENT_EXAMPLE_TX_PULL_H
#define CLIENT_EXAMPLE_TX_PULL_H

#include <vector>

#include "boltprotocol/message_defs.h"
#include "client_example_session.h"

namespace ClientTransaction {

    boltprotocol::BoltError pull_all_results_in_transaction(ClientSession& session, int64_t qid, std::vector<boltprotocol::RecordMessageParams>& out_records);

}  // namespace ClientTransaction

#endif  // CLIENT_EXAMPLE_TX_PULL_H