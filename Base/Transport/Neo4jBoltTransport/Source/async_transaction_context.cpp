#include "neo4j_bolt_transport/async_transaction_context.h"

#include "neo4j_bolt_transport/async_session_handle.h"  // For owner_session_ methods

namespace neo4j_bolt_transport {

    AsyncTransactionContext::AsyncTransactionContext(AsyncSessionHandle& session) : owner_session_(session) {
    }

    boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> AsyncTransactionContext::run_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters) {
        // This will call a method on AsyncSessionHandle to run a query *within* its active explicit transaction
        // The AsyncSessionHandle needs to manage the state that it's currently in an explicit transaction
        // initiated by the executeRead/WriteTransaction_async methods.
        co_return co_await owner_session_.run_query_in_transaction_async(cypher, parameters);
    }

    // Placeholder for run_stream_async
    // boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::unique_ptr<AsyncResultStream>>>
    // AsyncTransactionContext::run_stream_async(const std::string& cypher,
    //                                          const std::map<std::string, boltprotocol::Value>& parameters) {
    //     // Similar to run_async, but calls a streaming version on AsyncSessionHandle
    //     co_return co_await owner_session_.run_query_stream_in_transaction_async(cypher, parameters);
    // }

}  // namespace neo4j_bolt_transport