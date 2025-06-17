#include <iostream>  // For debug

#include "neo4j_bolt_transport/result_stream.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    std::tuple<boltprotocol::BoltError, std::string, boltprotocol::SuccessMessageParams> BoltResultStream::consume() {
        // std::cout << "[ResultStreamCONSUME " << this << "] Consuming stream." << std::endl;
        if (stream_failed_) {
            return {failure_reason_, failure_message_, final_pull_or_discard_summary_};
        }
        if (stream_fully_consumed_or_discarded_) {
            return {boltprotocol::BoltError::SUCCESS, "", final_pull_or_discard_summary_};
        }

        // _discard_all_remaining_records handles clearing local buffer and sending DISCARD if necessary.
        auto discard_result = _discard_all_remaining_records();

        // stream_fully_consumed_or_discarded_ is set by _discard_all_remaining_records
        // failure_state is also set by _discard_all_remaining_records if it fails

        if (discard_result.first != boltprotocol::BoltError::SUCCESS) {
            // _set_failure_state was called by _discard_all_remaining_records
            return {failure_reason_, failure_message_, final_pull_or_discard_summary_};
        }

        // Update session bookmarks from the final summary for auto-commit sessions.
        // This happens only if consume was successful and it was an auto-commit query.
        if (owner_session_ && !owner_session_->is_in_transaction() && !stream_failed_) {
            auto it_bookmark = final_pull_or_discard_summary_.metadata.find("bookmark");
            if (it_bookmark != final_pull_or_discard_summary_.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                owner_session_->update_bookmarks({std::get<std::string>(it_bookmark->second)});
            } else {
                // Only clear bookmarks if the operation was truly successful at the server
                // and no bookmark was returned. If the PULL/DISCARD failed with a server error,
                // existing bookmarks should probably be preserved.
                // This depends on how `final_pull_or_discard_summary_` is populated on failure.
                // For now, if no error, and no bookmark, clear.
                if (failure_reason_ == boltprotocol::BoltError::SUCCESS) {
                    owner_session_->update_bookmarks({});
                }
            }
        }
        // std::cout << "[ResultStreamCONSUME " << this << "] Consume successful." << std::endl;
        return {boltprotocol::BoltError::SUCCESS, "", final_pull_or_discard_summary_};
    }

}  // namespace neo4j_bolt_transport