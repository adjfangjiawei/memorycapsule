#ifndef CLIENT_EXAMPLE_TX_RUN_H
#define CLIENT_EXAMPLE_TX_RUN_H

#include <map>
#include <string>

#include "boltprotocol/message_defs.h"
#include "client_example_session.h"

namespace ClientTransaction {

    boltprotocol::BoltError run_query_in_transaction(ClientSession& session, const std::string& query, const std::map<std::string, boltprotocol::Value>& params,
                                                     int64_t& out_qid);  // Output parameter for query ID

}  // namespace ClientTransaction

#endif  // CLIENT_EXAMPLE_TX_RUN_H