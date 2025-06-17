#ifndef NEO4J_BOLT_TRANSPORT_RESULT_STREAM_H
#define NEO4J_BOLT_TRANSPORT_RESULT_STREAM_H

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "bolt_record.h"
#include "boltprotocol/message_defs.h"
#include "result_summary.h"  // <--- NEW

namespace neo4j_bolt_transport {

    class SessionHandle;  // Forward declaration

    class BoltResultStream {
      public:
        // Constructor now takes initial raw summary and connection info for typed summary
        BoltResultStream(SessionHandle* session,
                         std::optional<int64_t> query_id_for_streaming,
                         boltprotocol::SuccessMessageParams run_summary_params,  // Raw params from RUN
                         std::shared_ptr<const std::vector<std::string>> field_names_ptr,
                         std::vector<boltprotocol::RecordMessageParams> initial_records,
                         bool server_might_have_more,
                         const boltprotocol::versions::Version& bolt_version,          // For ResultSummary
                         bool utc_patch_active,                                        // For ResultSummary
                         const std::string& server_address_for_summary,                // For ResultSummary
                         const std::optional<std::string>& database_name_for_summary,  // For ResultSummary
                         boltprotocol::BoltError initial_error = boltprotocol::BoltError::SUCCESS,
                         const std::string& initial_error_message = "",
                         const std::optional<boltprotocol::FailureMessageParams>& initial_failure_details = std::nullopt);

        ~BoltResultStream();

        BoltResultStream(const BoltResultStream&) = delete;
        BoltResultStream& operator=(const BoltResultStream&) = delete;
        BoltResultStream(BoltResultStream&& other) noexcept;
        BoltResultStream& operator=(BoltResultStream&& other) noexcept;

        std::pair<boltprotocol::BoltError, std::string> has_next(bool& out_has_next);
        std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>> next();
        std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>> single();  // <--- NEW

        std::tuple<boltprotocol::BoltError, std::string, std::vector<BoltRecord>> list_all();
        std::tuple<boltprotocol::BoltError, std::string, ResultSummary> consume();  // <--- MODIFIED to return typed ResultSummary

        const ResultSummary& get_run_summary() const {
            return run_summary_typed_;
        }  // <--- MODIFIED
        const ResultSummary& get_final_summary() const {
            return final_summary_typed_;
        }  // <--- MODIFIED (after full consumption/discard)

        bool is_fully_consumed_or_failed() const;
        bool has_failed() const;
        boltprotocol::BoltError get_failure_reason() const;
        const std::string& get_failure_message() const;
        const boltprotocol::FailureMessageParams& get_failure_details() const;  // For raw server failure
        const std::vector<std::string>& field_names() const;

      private:
        friend class SessionHandle;
        friend class TransactionContext;

        std::pair<boltprotocol::BoltError, std::string> _fetch_more_records(int64_t n);
        std::pair<boltprotocol::BoltError, std::string> _discard_all_remaining_records();
        void _set_failure_state(boltprotocol::BoltError reason, std::string detailed_message, const std::optional<boltprotocol::FailureMessageParams>& details = std::nullopt);
        void _update_final_summary(boltprotocol::SuccessMessageParams&& pull_or_discard_raw_summary);

        SessionHandle* owner_session_;
        std::optional<int64_t> query_id_;

        std::deque<boltprotocol::RecordMessageParams> raw_record_buffer_;
        std::shared_ptr<const std::vector<std::string>> field_names_ptr_cache_;

        ResultSummary run_summary_typed_;    // Summary from RUN message (available immediately)
        ResultSummary final_summary_typed_;  // Summary from final PULL/DISCARD (available after consumption)
                                             // This needs careful initialization.

        boltprotocol::FailureMessageParams failure_details_raw_;  // Store raw failure

        bool server_has_more_records_ = false;
        bool initial_server_has_more_records_ = false;
        bool stream_fully_consumed_or_discarded_ = false;
        bool stream_failed_ = false;
        boltprotocol::BoltError failure_reason_ = boltprotocol::BoltError::SUCCESS;
        std::string failure_message_;
        bool is_first_pull_attempt_ = true;

        // For ResultSummary creation
        boltprotocol::versions::Version bolt_version_cache_;
        bool utc_patch_active_cache_;
        std::string server_address_cache_;
        std::optional<std::string> database_name_cache_;
    };

    // Inline simple getters
    inline bool BoltResultStream::is_fully_consumed_or_failed() const {
        return stream_fully_consumed_or_discarded_ || stream_failed_;
    }
    inline bool BoltResultStream::has_failed() const {
        return stream_failed_;
    }
    inline boltprotocol::BoltError BoltResultStream::get_failure_reason() const {
        return failure_reason_;
    }
    inline const std::string& BoltResultStream::get_failure_message() const {
        return failure_message_;
    }
    inline const boltprotocol::FailureMessageParams& BoltResultStream::get_failure_details() const {
        return failure_details_raw_;
    }

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_RESULT_STREAM_H