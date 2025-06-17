#include "neo4j_bolt_transport/neo4j_transaction_context.h"

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    TransactionContext::TransactionContext(SessionHandle& session) : owner_session_(session) {
    }

    std::pair<std::pair<boltprotocol::BoltError, std::string>, std::unique_ptr<BoltResultStream>> TransactionContext::run(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters) {
        // This method is called by user code *within* a lambda passed to executeRead/executeWrite.
        // The SessionHandle's execute_transaction_work_internal method ensures that an
        // explicit transaction is active on the SessionHandle before calling the user's lambda.
        if (!owner_session_.is_in_transaction()) {
            // This case should ideally not be reached if SessionHandle manages state correctly.
            std::string err_msg = "TransactionContext::run called, but SessionHandle is not in an active explicit transaction.";

            // Create a dummy failed BoltResultStream to return
            boltprotocol::SuccessMessageParams dummy_run_summary;  // Empty summary
            auto dummy_field_names = std::make_shared<const std::vector<std::string>>();
            std::vector<boltprotocol::RecordMessageParams> empty_records;

            auto failed_stream = std::make_unique<BoltResultStream>(&owner_session_,
                                                                    std::nullopt,  // No valid qid
                                                                    std::move(dummy_run_summary),
                                                                    dummy_field_names,
                                                                    std::move(empty_records),
                                                                    false  // No more records
            );
            // Use the internal method of BoltResultStream to set its failure state
            failed_stream->_set_failure_state(boltprotocol::BoltError::INVALID_ARGUMENT, err_msg);

            return {{boltprotocol::BoltError::INVALID_ARGUMENT, err_msg}, std::move(failed_stream)};
        }

        // Delegate to SessionHandle's run_query, which handles interaction with BoltPhysicalConnection.
        // The tx_metadata_override is nullopt because we are already inside a managed transaction;
        // metadata and timeout for this transaction were set when it began.
        return owner_session_.run_query(cypher, parameters, std::nullopt);
    }

    std::pair<boltprotocol::BoltError, std::string> TransactionContext::run_consume(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure) {
        if (!owner_session_.is_in_transaction()) {
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "TransactionContext::run_consume called, but SessionHandle is not in an active explicit transaction."};
        }
        // Delegate to SessionHandle's run_consume.
        return owner_session_.run_consume(cypher, parameters, out_summary, out_failure, std::nullopt);
    }

}  // namespace neo4j_bolt_transport