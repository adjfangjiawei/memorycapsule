#ifndef NEO4J_BOLT_TRANSPORT_ASYNC_SESSION_HANDLE_H
#define NEO4J_BOLT_TRANSPORT_ASYNC_SESSION_HANDLE_H

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "config/session_parameters.h"
#include "internal/async_types.h"
#include "result_summary.h"
// #include "async_result_stream.h" // Forward declare instead to avoid circular dependency if AsyncResultStream includes this back

// Conditional include for OpenSSL headers
#ifdef __has_include
#if __has_include(<openssl/ssl.h>)
// #include <openssl/ssl.h>
#define HAS_OPENSSL_SSL_H_ASYNC_SESSION_HEADER
#endif
#endif

namespace neo4j_bolt_transport {

    class Neo4jBoltTransport;
    class AsyncResultStream;  // Forward declaration

    class AsyncSessionHandle {
      public:
        AsyncSessionHandle(Neo4jBoltTransport* transport_manager, config::SessionParameters params, std::unique_ptr<internal::ActiveAsyncStreamContext> stream_ctx);
        ~AsyncSessionHandle();

        AsyncSessionHandle(const AsyncSessionHandle&) = delete;
        AsyncSessionHandle& operator=(const AsyncSessionHandle&) = delete;
        AsyncSessionHandle(AsyncSessionHandle&& other) noexcept;
        AsyncSessionHandle& operator=(AsyncSessionHandle&& other) noexcept;

        boost::asio::awaitable<boltprotocol::BoltError> close_async();

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> run_query_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters = {});

        // Asynchronously runs a query and returns a stream for iterating over results.
        // The returned unique_ptr will be null if the initial RUN fails.
        boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::unique_ptr<AsyncResultStream>>> run_query_stream_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters = {});

        bool is_valid() const;
        bool is_closed() const {
            return is_closed_.load(std::memory_order_acquire);
        }

        const config::SessionParameters& get_session_parameters() const {
            return session_params_;
        }
        // Giving access to the stream context for AsyncResultStream (friend or public getter)
        // For now, AsyncResultStream will be a friend or take ownership.
        // Let's make AsyncResultStream a friend for now or pass context.
        // Passing context to AsyncResultStream constructor is cleaner.
        const internal::ActiveAsyncStreamContext* get_stream_context_for_query() const {  // Renamed to avoid clash
            return stream_context_.get();
        }

      private:
        friend class AsyncResultStream;  // Allow AsyncResultStream to access private members if needed

        boost::asio::awaitable<boltprotocol::BoltError> send_goodbye_if_appropriate_async();
        void mark_closed();
        boltprotocol::RunMessageParams _prepare_run_message_params(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters);

        Neo4jBoltTransport* transport_manager_;
        config::SessionParameters session_params_;
        std::unique_ptr<internal::ActiveAsyncStreamContext> stream_context_;

        std::atomic<bool> is_closed_;
        std::atomic<bool> close_initiated_;

        boltprotocol::BoltError last_error_code_ = boltprotocol::BoltError::SUCCESS;
        std::string last_error_message_;
    };

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_ASYNC_SESSION_HANDLE_H