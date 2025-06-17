#include <iostream>  // For debug

#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // For error formatting
#include "neo4j_bolt_transport/result_stream.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    std::pair<boltprotocol::BoltError, std::string> BoltResultStream::_fetch_more_records(int64_t n) {
        if (!owner_session_ || !owner_session_->is_connection_valid()) {
            std::string msg = "Fetch records: Invalid session or connection.";
            _set_failure_state(boltprotocol::BoltError::NETWORK_ERROR, msg);
            return {failure_reason_, failure_message_};
        }
        // This check is important. If stream is already marked as failed or fully consumed (and not the first pull), don't try.
        if (stream_failed_ || (stream_fully_consumed_or_discarded_ && !is_first_pull_attempt_)) {
            return {failure_reason_ != boltprotocol::BoltError::SUCCESS ? failure_reason_ : boltprotocol::BoltError::UNKNOWN_ERROR, failure_message_};
        }
        // std::cout << "[ResultStreamFETCH " << this << "] Fetching " << n << ". QID: " << (query_id_ ? std::to_string(*query_id_) : "auto") << std::endl;

        std::vector<boltprotocol::RecordMessageParams> fetched_records;
        boltprotocol::SuccessMessageParams current_pull_summary;
        boltprotocol::FailureMessageParams server_failure_details;  // To capture if PULL results in FAILURE message

        std::optional<int64_t> qid_for_this_pull = query_id_;  // Use member qid_ (which is set for explicit TX)
                                                               // Session's _stream_pull_records handles nullopt qid for auto-commit

        auto pull_result_pair = owner_session_->_stream_pull_records(qid_for_this_pull, n, fetched_records, current_pull_summary);
        // The err_code from _stream_pull_records is the BoltError from the *message exchange* itself (IO, protocol)
        // OR it's the error code from a server FAILURE message if one was received for PULL.

        is_first_pull_attempt_ = false;

        if (pull_result_pair.first != boltprotocol::BoltError::SUCCESS) {
            // SessionHandle's _stream_pull_records already called _invalidate_session_due_to_connection_error
            // and BoltPhysicalConnection would have called _classify_and_set_server_failure if a FAILURE msg was received.
            // We need to get the *actual* server failure details if that was the case.
            // This implies _stream_pull_records should return more than just BoltError/string, or populate
            // a shared FailureMessageParams.
            // For now, use the message from the pair.
            // TODO: Improve how FailureMessageParams is propagated up from BoltPhysicalConnection through SessionHandle to here.
            boltprotocol::FailureMessageParams temp_failure_details_from_session;
            if (owner_session_ && owner_session_->is_connection_valid() && owner_session_->connection_) {  // Check connection exists
                // This is a temporary hack, ideally SessionHandle would pass FailureMessageParams up too
                // For now, if the connection has a server failure code, try to make a message.
                if (owner_session_->connection_->get_last_error() != boltprotocol::BoltError::SUCCESS && owner_session_->connection_->get_last_error() != pull_result_pair.first) {  // if conn error is more specific
                    temp_failure_details_from_session.metadata["message"] = boltprotocol::Value(owner_session_->connection_->get_last_error_message());
                }
            }
            _set_failure_state(pull_result_pair.first, pull_result_pair.second, temp_failure_details_from_session);
            return {failure_reason_, failure_message_};
        }

        // PULL message exchange was successful, and server returned a SUCCESS summary for PULL
        for (auto& rec : fetched_records) {
            raw_record_buffer_.push_back(std::move(rec));
        }

        final_pull_or_discard_summary_ = current_pull_summary;
        auto it_has_more = current_pull_summary.metadata.find("has_more");
        if (it_has_more != current_pull_summary.metadata.end() && std::holds_alternative<bool>(it_has_more->second)) {
            server_has_more_records_ = std::get<bool>(it_has_more->second);
        } else {
            server_has_more_records_ = false;
        }

        if (!server_has_more_records_ && raw_record_buffer_.empty()) {
            stream_fully_consumed_or_discarded_ = true;
        }
        // std::cout << "[ResultStreamFETCH " << this << "] Fetched " << fetched_records.size()
        //           << ". Buffer: " << raw_record_buffer_.size() << ". ServerMore: " << server_has_more_records_ << std::endl;
        return {boltprotocol::BoltError::SUCCESS, ""};
    }

    std::pair<boltprotocol::BoltError, std::string> BoltResultStream::_discard_all_remaining_records() {
        // std::cout << "[ResultStreamDISCARD " << this << "] Discarding. QID: " << (query_id_ ? std::to_string(*query_id_) : "auto") << std::endl;
        if (!owner_session_ || !owner_session_->is_connection_valid()) {
            _set_failure_state(boltprotocol::BoltError::NETWORK_ERROR, "Discard: Invalid session/connection.");
            return {failure_reason_, failure_message_};
        }
        if (stream_failed_ || stream_fully_consumed_or_discarded_) {
            return {failure_reason_ != boltprotocol::BoltError::SUCCESS ? failure_reason_ : boltprotocol::BoltError::SUCCESS, failure_message_};
        }

        raw_record_buffer_.clear();

        // If server_has_more_records_ is already false (from a previous PULL that fetched everything or a DISCARD)
        // AND we are past the first pull/discard attempt (is_first_pull_attempt_ is false),
        // then there's nothing on the server to discard.
        if (!server_has_more_records_ && !is_first_pull_attempt_) {
            stream_fully_consumed_or_discarded_ = true;
            // final_pull_or_discard_summary_ should be from the last PULL/DISCARD.
            return {boltprotocol::BoltError::SUCCESS, ""};
        }
        // If it's the first action and the initial run_summary indicated no records (server_has_more_records_ was false from constructor)
        if (is_first_pull_attempt_ && !server_has_more_records_) {
            stream_fully_consumed_or_discarded_ = true;
            final_pull_or_discard_summary_ = run_summary_;
            return {boltprotocol::BoltError::SUCCESS, ""};
        }

        boltprotocol::SuccessMessageParams discard_summary_from_server;
        std::optional<int64_t> qid_for_discard = query_id_;  // Use member qid_ for explicit, session handles auto

        auto discard_result_pair = owner_session_->_stream_discard_records(qid_for_discard, -1, discard_summary_from_server);
        is_first_pull_attempt_ = false;
        stream_fully_consumed_or_discarded_ = true;

        if (discard_result_pair.first != boltprotocol::BoltError::SUCCESS) {
            _set_failure_state(discard_result_pair.first, discard_result_pair.second);  // TODO: Get FailureMessageParams if DISCARD results in FAILURE
            return {failure_reason_, failure_message_};
        }

        final_pull_or_discard_summary_ = discard_summary_from_server;
        server_has_more_records_ = false;
        // std::cout << "[ResultStreamDISCARD " << this << "] Discard successful." << std::endl;
        return {boltprotocol::BoltError::SUCCESS, ""};
    }

}  // namespace neo4j_bolt_transport