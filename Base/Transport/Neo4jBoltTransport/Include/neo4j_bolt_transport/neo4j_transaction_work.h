#ifndef NEO4J_BOLT_TRANSPORT_NEO4J_TRANSACTION_WORK_H
#define NEO4J_BOLT_TRANSPORT_NEO4J_TRANSACTION_WORK_H

#include <functional>
#include <string>
#include <utility>  // For std::pair

#include "boltprotocol/bolt_errors_versions.h"  // For BoltError

namespace neo4j_bolt_transport {

    // Forward declare TransactionContext (defined in neo4j_transaction_context.h)
    class TransactionContext;

    // Result type for transaction work: error code and detailed message string.
    using TransactionWorkResult = std::pair<boltprotocol::BoltError, std::string>;

    // Type for user-provided lambda to execute within a transaction.
    // It receives a TransactionContext to perform database operations.
    // It should return SUCCESS if work is done, or an error code + message if app logic fails.
    using TransactionWork = std::function<TransactionWorkResult(TransactionContext& tx)>;

    // REMOVED conflicting TransactionContext class definition from here.
    // The primary definition is in neo4j_transaction_context.h

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_NEO4J_TRANSACTION_WORK_H