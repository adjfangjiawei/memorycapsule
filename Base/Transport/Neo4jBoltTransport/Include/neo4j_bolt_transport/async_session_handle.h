#ifndef NEO4J_BOLT_TRANSPORT_ASYNC_SESSION_HANDLE_H
#define NEO4J_BOLT_TRANSPORT_ASYNC_SESSION_HANDLE_H

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "async_transaction_context.h"
#include "boltprotocol/message_defs.h"
#include "config/session_parameters.h"
#include "internal/async_types.h"
#include "neo4j_bolt_transport/config/transport_config.h"
#include "result_summary.h"

// Conditional include for OpenSSL headers
#ifdef __has_include
#if __has_include(<openssl/ssl.h>)
#define HAS_OPENSSL_SSL_H_ASYNC_SESSION_HEADER
#endif
#endif

namespace neo4j_bolt_transport {

    class Neo4jBoltTransport;
    class AsyncResultStream;

    struct AsyncTransactionConfigOverrides {
        std::optional<std::map<std::string, boltprotocol::Value>> metadata;
        std::optional<std::chrono::milliseconds> timeout;
    };

    class AsyncSessionHandle {
      public:
        AsyncSessionHandle(Neo4jBoltTransport* transport_manager, config::SessionParameters params, std::unique_ptr<internal::ActiveAsyncStreamContext> stream_ctx);
        ~AsyncSessionHandle();

        AsyncSessionHandle(const AsyncSessionHandle&) = delete;
        AsyncSessionHandle& operator=(const AsyncSessionHandle&) = delete;
        AsyncSessionHandle(AsyncSessionHandle&& other) noexcept;
        AsyncSessionHandle& operator=(AsyncSessionHandle&& other) noexcept;

        boost::asio::awaitable<boltprotocol::BoltError> close_async();

        // --- Auto-commit Query Execution ---
        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> run_query_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters = {});

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::unique_ptr<AsyncResultStream>>> run_query_stream_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters = {});

        // --- Explicit Transaction Management ---
        boost::asio::awaitable<boltprotocol::BoltError> begin_transaction_async(const std::optional<AsyncTransactionConfigOverrides>& tx_config = std::nullopt);

        boost::asio::awaitable<boltprotocol::BoltError> commit_transaction_async();

        boost::asio::awaitable<boltprotocol::BoltError> rollback_transaction_async();

        bool is_in_transaction() const {
            return in_explicit_transaction_.load(std::memory_order_acquire);
        }

        // --- Query Execution within an Explicit Transaction (used by AsyncTransactionContext) ---
        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> run_query_in_transaction_async(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters = {});

        // --- Managed Transaction Functions ---
        boost::asio::awaitable<TransactionWorkResult> execute_read_transaction_async(AsyncTransactionWork work, const std::optional<AsyncTransactionConfigOverrides>& tx_config = std::nullopt);

        boost::asio::awaitable<TransactionWorkResult> execute_write_transaction_async(AsyncTransactionWork work, const std::optional<AsyncTransactionConfigOverrides>& tx_config = std::nullopt);

        // --- Bookmark Management ---
        const std::vector<std::string>& get_last_bookmarks() const;
        // Typically, bookmarks are updated internally based on server responses.
        // A public set_bookmarks might be for advanced scenarios or testing.
        // void set_bookmarks(std::vector<std::string> bookmarks);

        bool is_valid() const;
        bool is_closed() const {
            return is_closed_.load(std::memory_order_acquire);
        }

        const config::SessionParameters& get_session_parameters() const {
            return session_params_;
        }
        const internal::ActiveAsyncStreamContext* get_stream_context_for_query() const {
            return stream_context_.get();
        }

      private:
        friend class AsyncResultStream;
        friend class AsyncTransactionContext;

        boost::asio::awaitable<boltprotocol::BoltError> send_goodbye_if_appropriate_async();
        void mark_closed();
        boltprotocol::RunMessageParams _prepare_run_message_params(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters);
        boltprotocol::BeginMessageParams _prepare_begin_message_params(const std::optional<AsyncTransactionConfigOverrides>& tx_config);

        boost::asio::awaitable<TransactionWorkResult> _execute_transaction_work_internal_async(AsyncTransactionWork work, config::AccessMode mode_hint, const std::optional<AsyncTransactionConfigOverrides>& tx_config);

        // Internal method to update bookmarks from a SUCCESS summary
        void _update_bookmarks_from_summary(const boltprotocol::SuccessMessageParams& summary_params);

        Neo4jBoltTransport* transport_manager_;
        config::SessionParameters session_params_;
        std::unique_ptr<internal::ActiveAsyncStreamContext> stream_context_;

        std::vector<std::string> current_bookmarks_;  // Stores the last known good bookmarks

        std::atomic<bool> is_closed_;
        std::atomic<bool> close_initiated_;
        std::atomic<bool> in_explicit_transaction_;

        std::optional<int64_t> last_tx_run_qid_;

        boltprotocol::BoltError last_error_code_ = boltprotocol::BoltError::SUCCESS;
        std::string last_error_message_;
    };

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_ASYNC_SESSION_HANDLE_H