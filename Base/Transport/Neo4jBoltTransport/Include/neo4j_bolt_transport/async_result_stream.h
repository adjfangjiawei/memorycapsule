#ifndef NEO4J_BOLT_TRANSPORT_ASYNC_RESULT_STREAM_H
#define NEO4J_BOLT_TRANSPORT_ASYNC_RESULT_STREAM_H

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bolt_record.h"
#include "boltprotocol/message_defs.h"
#include "config/session_parameters.h"  // For default_fetch_size
#include "internal/async_types.h"       // For ActiveAsyncStreamContext
#include "result_summary.h"

namespace neo4j_bolt_transport {

    class AsyncSessionHandle;  // Forward declaration

    class AsyncResultStream {
      public:
        // Constructor takes the active stream context, initial run summary, field names,
        // initial (pipelined) records, and server's indication of more records.
        AsyncResultStream(AsyncSessionHandle* owner_session,                               // Non-owning pointer to the session for context
                          std::unique_ptr<internal::ActiveAsyncStreamContext> stream_ctx,  // Takes ownership
                          std::optional<int64_t> query_id,
                          boltprotocol::SuccessMessageParams run_summary_params_raw,
                          std::shared_ptr<const std::vector<std::string>> field_names,
                          std::vector<boltprotocol::RecordMessageParams> initial_records_raw,
                          bool server_had_more_after_run,
                          const config::SessionParameters& session_config  // To get fetch_size
        );

        ~AsyncResultStream();

        AsyncResultStream(const AsyncResultStream&) = delete;
        AsyncResultStream& operator=(const AsyncResultStream&) = delete;
        AsyncResultStream(AsyncResultStream&& other) noexcept;
        AsyncResultStream& operator=(AsyncResultStream&& other) noexcept;

        // Asynchronously fetches the next record from the stream.
        // Returns: {error, error_message, optional_record}
        boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>>> next_async();

        // Asynchronously consumes all remaining records and returns the final summary.
        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> consume_async();

        // Returns the summary information obtained from the initial RUN message.
        // This is available immediately after run_query_stream_async returns.
        const ResultSummary& run_summary() const {
            return run_summary_typed_;
        }

        // Returns the final summary information after the stream has been fully consumed or an error occurred.
        // This might be the same as run_summary if no PULL was needed or if consume_async hasn't completed.
        const ResultSummary& final_summary() const {
            return final_summary_typed_;
        }

        bool is_open() const;  // Checks if the stream context is valid and open
        bool has_failed() const {
            return stream_failed_.load(std::memory_order_acquire);
        }
        boltprotocol::BoltError get_failure_reason() const {
            return failure_reason_.load(std::memory_order_acquire);
        }
        const std::string& get_failure_message() const {
            return failure_message_;
        }  // Not atomic, accessed after failure typically

        const std::vector<std::string>& field_names() const;

      private:
        friend class AsyncSessionHandle;

        // Asynchronously checks if there are more records on the server and fetches them if needed.
        // This is an internal helper for next_async.
        // Returns: {error, error_message, has_more_locally_or_fetched}
        boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, bool>> ensure_records_buffered_async();

        boost::asio::awaitable<boltprotocol::BoltError> send_pull_async(int64_t n, boltprotocol::SuccessMessageParams& out_pull_summary_raw);

        boost::asio::awaitable<boltprotocol::BoltError> send_discard_async(int64_t n, boltprotocol::SuccessMessageParams& out_discard_summary_raw);

        void set_failure_state(boltprotocol::BoltError reason, std::string detailed_message, const std::optional<boltprotocol::FailureMessageParams>& details = std::nullopt);
        void update_final_summary(boltprotocol::SuccessMessageParams&& pull_or_discard_raw_summary);

        AsyncSessionHandle* owner_session_;                                   // Non-owning, for logger and transport access
        std::unique_ptr<internal::ActiveAsyncStreamContext> stream_context_;  // Owns the stream
        std::optional<int64_t> query_id_;                                     // From RUN or for explicit TX
        config::SessionParameters session_config_cache_;                      // Cache for fetch_size etc.

        std::deque<boltprotocol::RecordMessageParams> raw_record_buffer_;
        std::shared_ptr<const std::vector<std::string>> field_names_ptr_cache_;

        ResultSummary run_summary_typed_;
        ResultSummary final_summary_typed_;  // Initially same as run_summary, updated by PULL/DISCARD

        std::atomic<bool> server_has_more_records_after_last_pull_;
        bool initial_server_has_more_after_run_;  // From RUN summary, to know if first PULL is needed

        std::atomic<bool> stream_fully_consumed_or_discarded_;
        std::atomic<bool> stream_failed_;
        std::atomic<boltprotocol::BoltError> failure_reason_;
        std::string failure_message_;  // Protected by stream_failed_ or used after failure

        bool is_first_fetch_attempt_ = true;  // To manage initial PULL vs subsequent PULLs
    };

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_ASYNC_RESULT_STREAM_H