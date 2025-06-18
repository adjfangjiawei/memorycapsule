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

// Conditional include for OpenSSL headers
#ifdef __has_include
#if __has_include(<openssl/ssl.h>)
// #include <openssl/ssl.h> // Not directly needed in this header, but good to keep track if implementations depend on it
#define HAS_OPENSSL_SSL_H_ASYNC_SESSION_HEADER
#endif
#endif

namespace neo4j_bolt_transport {

    class Neo4jBoltTransport;

    class AsyncSessionHandle {
      public:
        AsyncSessionHandle(Neo4jBoltTransport* transport_manager, config::SessionParameters params, std::unique_ptr<internal::ActiveAsyncStreamContext> stream_ctx);
        ~AsyncSessionHandle();

        AsyncSessionHandle(const AsyncSessionHandle&) = delete;
        AsyncSessionHandle& operator=(const AsyncSessionHandle&) = delete;
        AsyncSessionHandle(AsyncSessionHandle&& other) noexcept;
        AsyncSessionHandle& operator=(AsyncSessionHandle&& other) noexcept;

        boost::asio::awaitable<boltprotocol::BoltError> close_async();

        // This version consumes all records and returns the final summary.
        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> run_query_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters = {});

        // Future additions could include:
        // boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::unique_ptr<AsyncResultStream>>>
        // run_query_stream_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters = {});

        bool is_valid() const;
        bool is_closed() const {
            return is_closed_.load(std::memory_order_acquire);
        }

        const config::SessionParameters& get_session_parameters() const {
            return session_params_;
        }
        const internal::ActiveAsyncStreamContext* get_stream_context() const {
            return stream_context_.get();
        }

      private:
        boost::asio::awaitable<boltprotocol::BoltError> send_goodbye_if_appropriate_async();
        void mark_closed();

        // Helper for preparing RUN message parameters common to async execution
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