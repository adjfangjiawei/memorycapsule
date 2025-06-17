#ifndef NEO4J_BOLT_TRANSPORT_NEO4J_TRANSACTION_CONTEXT_H
#define NEO4J_BOLT_TRANSPORT_NEO4J_TRANSACTION_CONTEXT_H

#include <functional>  // For std::function
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"  // For Value, SuccessMessageParams, FailureMessageParams
// BoltRecord and BoltResultStream are needed for the 'run' method's return type
#include "bolt_record.h"
#include "result_stream.h"

namespace neo4j_bolt_transport {

    class SessionHandle;  // Forward declaration

    // Result type for transaction work: error code and detailed message string.
    using TransactionWorkResult = std::pair<boltprotocol::BoltError, std::string>;

    // TransactionContext is passed to user-provided transaction functions (lambdas).
    // It provides methods to execute queries within the scope of the managed transaction.
    class TransactionContext {
      public:
        // Constructor taking a non-owning pointer to the SessionHandle that manages this transaction.
        // The SessionHandle must outlive the TransactionContext.
        explicit TransactionContext(SessionHandle& session);
        virtual ~TransactionContext() = default;  // Good practice for base classes if inherited

        // Executes a query within the current transaction.
        // Returns a pair: { {Error, ErrorMsg}, ResultStreamUniquePtr }.
        // The ResultStream unique_ptr is null if an error occurred before streaming could start.
        std::pair<std::pair<boltprotocol::BoltError, std::string>, std::unique_ptr<BoltResultStream>> run(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters = {}  // Default empty parameters
        );

        // Executes a query and consumes its result, returning only the summary.
        // Useful for DML statements (CREATE, MERGE, DELETE, SET).
        // Returns {Error, ErrorMsg}. out_summary and out_failure are populated.
        std::pair<boltprotocol::BoltError, std::string> run_consume(const std::string& cypher,
                                                                    const std::map<std::string, boltprotocol::Value>& parameters,  // No default for params here
                                                                    boltprotocol::SuccessMessageParams& out_summary,
                                                                    boltprotocol::FailureMessageParams& out_failure);

        // Note: A full-fledged TransactionContext in official drivers often mirrors
        // many methods of the Session object (like run, commit, rollback, close).
        // However, for the managed transaction function pattern, the Session handles
        // commit/rollback/close based on the lambda's outcome.
        // If we want the lambda to have more control, these methods could be added here,
        // and they would signal the Session to perform the action.
        // For now, we keep it to query execution.

        // boltprotocol::BoltError commit(); // Example: Signals the managing Session to commit
        // boltprotocol::BoltError rollback(); // Example: Signals the managing Session to rollback
        // bool is_open() const; // Example: Checks if the underlying transaction is still active

      private:
        SessionHandle& owner_session_;  // Non-owning reference to the session managing the transaction
    };

    // Type for user-provided lambda to execute within a transaction.
    // It receives a TransactionContext to perform database operations.
    // It should return a TransactionWorkResult indicating success or application-level failure.
    using TransactionWork = std::function<TransactionWorkResult(TransactionContext& tx)>;

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_NEO4J_TRANSACTION_CONTEXT_H