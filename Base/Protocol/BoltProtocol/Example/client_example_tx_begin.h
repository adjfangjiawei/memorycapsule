#ifndef CLIENT_EXAMPLE_TX_BEGIN_H
#define CLIENT_EXAMPLE_TX_BEGIN_H

#include "boltprotocol/message_defs.h"  // For BoltError
#include "client_example_session.h"     // For ClientSession

namespace ClientTransaction {

    boltprotocol::BoltError begin_transaction(ClientSession& session);

}  // namespace ClientTransaction

#endif  // CLIENT_EXAMPLE_TX_BEGIN_H