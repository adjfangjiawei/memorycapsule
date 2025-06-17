#include <iostream>

#include "neo4j_bolt_transport/result_stream.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    std::tuple<boltprotocol::BoltError, std::string, ResultSummary> BoltResultStream::consume() {
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        if (logger) logger->trace("[ResultStreamCONSUME {}] Consuming stream.", (void*)this);

        if (stream_failed_) {
            // If failed, final_summary_typed_ might not be meaningful or might be from RUN.
            // Return run_summary_typed_ or a default-constructed ResultSummary if even that is bad.
            // For now, return existing final_summary_typed_ which should be default/run summary.
            return {failure_reason_, failure_message_, final_summary_typed_};
        }
        if (stream_fully_consumed_or_discarded_) {
            return {boltprotocol::BoltError::SUCCESS, "", final_summary_typed_};
        }

        auto discard_result_pair = _discard_all_remaining_records();  // This will update final_summary_typed_ internally via _update_final_summary

        if (discard_result_pair.first != boltprotocol::BoltError::SUCCESS) {
            // _set_failure_state was called by _discard_all_remaining_records
            // final_summary_typed_ might reflect the RUN summary in this case.
            return {failure_reason_, failure_message_, final_summary_typed_};
        }

        // Update session bookmarks from the final summary for auto-commit sessions.
        // This happens only if consume was successful and it was an auto-commit query.
        if (owner_session_ && !owner_session_->is_in_transaction() && !stream_failed_) {
            auto it_bookmark = final_summary_typed_.raw_params().metadata.find("bookmark");
            if (it_bookmark != final_summary_typed_.raw_params().metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                owner_session_->update_bookmarks({std::get<std::string>(it_bookmark->second)});
            } else {
                if (failure_reason_ == boltprotocol::BoltError::SUCCESS) {  // Only clear if server operation was success
                    owner_session_->update_bookmarks({});
                }
            }
        }
        if (logger) logger->trace("[ResultStreamCONSUME {}] Consume successful.", (void*)this);
        return {boltprotocol::BoltError::SUCCESS, "", final_summary_typed_};
    }

}  // namespace neo4j_bolt_transport