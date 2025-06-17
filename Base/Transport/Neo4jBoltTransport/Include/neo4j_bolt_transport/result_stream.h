#ifndef NEO4J_BOLT_TRANSPORT_RESULT_STREAM_H
#define NEO4J_BOLT_TRANSPORT_RESULT_STREAM_H

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <tuple>    // For std::tuple in consume()
#include <utility>  // For std::pair
#include <vector>

#include "bolt_record.h"  // Include BoltRecord
#include "boltprotocol/message_defs.h"

namespace neo4j_bolt_transport {

    class SessionHandle;

    class BoltResultStream {
      public:
        // Constructor now can accept initial error state for failed stream creation
        BoltResultStream(SessionHandle* session,
                         std::optional<int64_t> query_id_for_streaming,
                         boltprotocol::SuccessMessageParams run_summary,
                         std::shared_ptr<const std::vector<std::string>> field_names_ptr,
                         std::vector<boltprotocol::RecordMessageParams> initial_records,
                         bool server_might_have_more,
                         boltprotocol::BoltError initial_error = boltprotocol::BoltError::SUCCESS,
                         const std::string& initial_error_message = "",
                         const std::optional<boltprotocol::FailureMessageParams>& initial_failure_details = std::nullopt);

        ~BoltResultStream();

        BoltResultStream(const BoltResultStream&) = delete;
        BoltResultStream& operator=(const BoltResultStream&) = delete;
        BoltResultStream(BoltResultStream&& other) noexcept;
        BoltResultStream& operator=(BoltResultStream&& other) noexcept;

        // Checks if there are more records available or might be available.
        // Returns SUCCESS if check is possible, error otherwise. Detailed error in out_message.
        std::pair<boltprotocol::BoltError, std::string> has_next(bool& out_has_next);

        // Fetches the next record.
        // Returns {SUCCESS, BoltRecord} or {error_code, std::nullopt} with error message in string.
        std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>> next();

        // Fetches all remaining records.
        // Returns {SUCCESS, records_vector} or {error_code, empty_vector} with error message.
        std::tuple<boltprotocol::BoltError, std::string, std::vector<BoltRecord>> list_all();

        // Consumes all remaining records and returns the final summary.
        // Return type changed to tuple to include error message string.
        std::tuple<boltprotocol::BoltError, std::string, boltprotocol::SuccessMessageParams> consume();

        const boltprotocol::SuccessMessageParams& get_run_summary() const {
            return run_summary_;
        }
        const boltprotocol::SuccessMessageParams& get_final_summary() const {
            return final_pull_or_discard_summary_;
        }
        bool is_fully_consumed_or_failed() const {
            return stream_fully_consumed_or_discarded_ || stream_failed_;
        }

        bool has_failed() const;                                                // Implementation in .cpp or inline here
        boltprotocol::BoltError get_failure_reason() const;                     // Implementation in .cpp or inline here
        const std::string& get_failure_message() const;                         // Implementation in .cpp or inline here
        const boltprotocol::FailureMessageParams& get_failure_details() const;  // Implementation in .cpp or inline here

        const std::vector<std::string>& field_names() const;

      private:
        friend class SessionHandle;
        friend class TransactionContext;  // To allow TransactionContext to call _set_failure_state if needed, though direct construction is better.

        std::pair<boltprotocol::BoltError, std::string> _fetch_more_records(int64_t n);
        std::pair<boltprotocol::BoltError, std::string> _discard_all_remaining_records();

        void _set_failure_state(boltprotocol::BoltError reason, std::string detailed_message, const std::optional<boltprotocol::FailureMessageParams>& details = std::nullopt);

        SessionHandle* owner_session_;
        std::optional<int64_t> query_id_;

        std::deque<boltprotocol::RecordMessageParams> raw_record_buffer_;
        std::shared_ptr<const std::vector<std::string>> field_names_ptr_cache_;

        boltprotocol::SuccessMessageParams run_summary_;
        boltprotocol::SuccessMessageParams final_pull_or_discard_summary_;
        boltprotocol::FailureMessageParams failure_details_;

        bool server_has_more_records_ = false;          // Reflects server's has_more flag from last PULL/DISCARD summary
        bool initial_server_has_more_records_ = false;  // Reflects server's has_more after RUN (or if records were pipelined)

        bool stream_fully_consumed_or_discarded_ = false;
        bool stream_failed_ = false;
        boltprotocol::BoltError failure_reason_ = boltprotocol::BoltError::SUCCESS;
        std::string failure_message_;
        bool is_first_pull_attempt_ = true;  // True if no PULL or DISCARD has been attempted yet.
    };

    // Inline definitions for simple getters if result_stream_state.cpp is removed/emptied for these
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
        return failure_details_;
    }

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_RESULT_STREAM_H