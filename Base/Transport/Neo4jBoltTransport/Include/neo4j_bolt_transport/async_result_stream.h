#ifndef NEO4J_BOLT_TRANSPORT_ASYNC_RESULT_STREAM_H
#define NEO4J_BOLT_TRANSPORT_ASYNC_RESULT_STREAM_H

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "bolt_record.h"
#include "boltprotocol/message_defs.h"
#include "config/session_parameters.h"
#include "internal/async_types.h"
#include "result_summary.h"

namespace neo4j_bolt_transport {

    class AsyncSessionHandle;

    class AsyncResultStream {
      public:
        AsyncResultStream(AsyncSessionHandle* owner_session,
                          std::unique_ptr<internal::ActiveAsyncStreamContext> stream_ctx,
                          std::optional<int64_t> query_id,
                          boltprotocol::SuccessMessageParams run_summary_params_raw,
                          std::shared_ptr<const std::vector<std::string>> field_names,
                          std::vector<boltprotocol::RecordMessageParams> initial_records_raw,
                          bool server_had_more_after_run,
                          const config::SessionParameters& session_config,
                          bool is_auto_commit  // New parameter
        );

        ~AsyncResultStream();

        AsyncResultStream(const AsyncResultStream&) = delete;
        AsyncResultStream& operator=(const AsyncResultStream&) = delete;
        AsyncResultStream(AsyncResultStream&& other) noexcept;
        AsyncResultStream& operator=(AsyncResultStream&& other) noexcept;

        boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>>> next_async();

        boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> consume_async();

        boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>>> single_async();

        boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, std::vector<BoltRecord>>> list_all_async();

        const ResultSummary& run_summary() const {
            return run_summary_typed_;
        }
        const ResultSummary& final_summary() const {
            return final_summary_typed_;
        }

        bool is_open() const;
        bool has_failed() const {
            return stream_failed_.load(std::memory_order_acquire);
        }
        boltprotocol::BoltError get_failure_reason() const {
            return failure_reason_.load(std::memory_order_acquire);
        }
        const std::string& get_failure_message() const {
            return failure_message_;
        }

        const std::vector<std::string>& field_names() const;

      private:
        friend class AsyncSessionHandle;

        boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, bool>> ensure_records_buffered_async();

        boost::asio::awaitable<boltprotocol::BoltError> send_pull_async(int64_t n, boltprotocol::SuccessMessageParams& out_pull_summary_raw);

        boost::asio::awaitable<boltprotocol::BoltError> send_discard_async(int64_t n, boltprotocol::SuccessMessageParams& out_discard_summary_raw);

        void set_failure_state(boltprotocol::BoltError reason, std::string detailed_message, const std::optional<boltprotocol::FailureMessageParams>& details = std::nullopt);
        void update_final_summary(boltprotocol::SuccessMessageParams&& pull_or_discard_raw_summary);
        void try_update_session_bookmarks_on_stream_end();  // New helper

        AsyncSessionHandle* owner_session_;
        std::unique_ptr<internal::ActiveAsyncStreamContext> stream_context_;
        std::optional<int64_t> query_id_;
        config::SessionParameters session_config_cache_;
        bool is_auto_commit_;  // New member to track if this stream is from an auto-commit query

        std::deque<boltprotocol::RecordMessageParams> raw_record_buffer_;
        std::shared_ptr<const std::vector<std::string>> field_names_ptr_cache_;

        ResultSummary run_summary_typed_;
        ResultSummary final_summary_typed_;

        std::atomic<bool> server_has_more_records_after_last_pull_;
        bool initial_server_has_more_after_run_;

        std::atomic<bool> stream_fully_consumed_or_discarded_;
        std::atomic<bool> stream_failed_;
        std::atomic<boltprotocol::BoltError> failure_reason_;
        std::string failure_message_;

        bool is_first_fetch_attempt_ = true;
    };

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_ASYNC_RESULT_STREAM_H