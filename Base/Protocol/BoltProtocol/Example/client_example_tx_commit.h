#ifndef CLIENT_EXAMPLE_TX_COMMIT_H
#define CLIENT_EXAMPLE_TX_COMMIT_H

#include "boltprotocol/message_defs.h"
#include "client_example_session.h"

namespace ClientTransaction {

    boltprotocol::BoltError commit_transaction(ClientSession& session);
    // boltprotocol::BoltError rollback_transaction(ClientSession& session); // For future

}  // namespace ClientTransaction

#endif  // CLIENT_EXAMPLE_TX_COMMIT_H