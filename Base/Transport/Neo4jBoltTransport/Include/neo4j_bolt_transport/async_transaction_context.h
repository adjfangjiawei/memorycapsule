#ifndef NEO4J_BOLT_TRANSPORT_ASYNC_TRANSACTION_CONTEXT_H
#define NEO4J_BOLT_TRANSPORT_ASYNC_TRANSACTION_CONTEXT_H

#include <boost/asio/awaitable.hpp>
#include <map>
#include <memory>
#include <optional>  // For std::optional
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "neo4j_bolt_transport/result_summary.h"  // For ResultSummary
// Forward declare AsyncResultStream if run_async returns it
// namespace neo4j_bolt_transport { class AsyncResultStream; }

namespace neo4j_bolt_transport {

    class AsyncSessionHandle;  // Forward declaration

    // Result type for transaction work (same as synchronous for now)
    using TransactionWorkResult = std::pair<boltprotocol::BoltError, std::string>;

    class AsyncTransactionContext {
      public:
        explicit AsyncTransactionContext(AsyncSessionHandle& session);

        // Asynchronously executes a query within the current managed transaction.
        // This version consumes the result and returns a summary.
        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> run_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters = {});

        // Placeholder for a streaming version within an async transaction
        // boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::unique_ptr<AsyncResultStream>>>
        // run_stream_async(const std::string& cypher,
        //                  const std::map<std::string, boltprotocol::Value>& parameters = {});

      private:
        AsyncSessionHandle& owner_session_;
    };

    // Type for user-provided lambda to execute within an asynchronous managed transaction.
    using AsyncTransactionWork = std::function<boost::asio::awaitable<TransactionWorkResult>(AsyncTransactionContext& tx)>;

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_ASYNC_TRANSACTION_CONTEXT_H