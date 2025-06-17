#include <iostream>  // For debug, replace with logging
#include <utility>   // For std::move

#include "neo4j_bolt_transport/result_stream.h"
#include "neo4j_bolt_transport/session_handle.h"  // For SessionHandle access for logging via connection

namespace neo4j_bolt_transport {

    std::pair<boltprotocol::BoltError, std::string> BoltResultStream::has_next(bool& out_has_next) {
        out_has_next = false;
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;

        if (stream_failed_) {
            if (logger) logger->trace("[ResultStream {}] has_next: Stream already failed. Reason: {}", (void*)this, static_cast<int>(failure_reason_));
            return {failure_reason_, failure_message_};
        }
        if (stream_fully_consumed_or_discarded_) {
            if (logger) logger->trace("[ResultStream {}] has_next: Stream fully consumed/discarded.", (void*)this);
            return {boltprotocol::BoltError::SUCCESS, ""};
        }
        if (!raw_record_buffer_.empty()) {
            if (logger) logger->trace("[ResultStream {}] has_next: Records in buffer.", (void*)this);
            out_has_next = true;
            return {boltprotocol::BoltError::SUCCESS, ""};
        }

        // Buffer is empty. Check if server might have more.
        // server_has_more_records_ is flag from last PULL/DISCARD.
        // is_first_pull_attempt_ is true if no PULL/DISCARD made yet.
        // initial_server_has_more_records_ is from RUN response.
        bool effectively_has_more_on_server = is_first_pull_attempt_ ? initial_server_has_more_records_ : server_has_more_records_;

        if (!effectively_has_more_on_server) {
            if (logger) logger->trace("[ResultStream {}] has_next: Buffer empty, server indicates no more records.", (void*)this);
            stream_fully_consumed_or_discarded_ = true;
            return {boltprotocol::BoltError::SUCCESS, ""};
        }

        // Determine fetch size
        int64_t fetch_n = 1000;  // Default fetch size
        if (owner_session_) {    // Check owner_session_ before accessing its members
            fetch_n = (owner_session_->session_params_.default_fetch_size > 0 || owner_session_->session_params_.default_fetch_size == -1) ? owner_session_->session_params_.default_fetch_size : 1000;
        }

        if (logger) logger->trace("[ResultStream {}] has_next: Buffer empty, attempting to fetch {} records.", (void*)this, fetch_n);
        auto fetch_result = _fetch_more_records(fetch_n);  // Implemented in result_stream_fetching.cpp

        if (fetch_result.first != boltprotocol::BoltError::SUCCESS) {
            // _fetch_more_records already called _set_failure_state and logged
            return fetch_result;
        }

        out_has_next = !raw_record_buffer_.empty();
        if (!out_has_next && !server_has_more_records_) {  // Fetched, buffer still empty, and PULL confirmed no more
            if (logger) logger->trace("[ResultStream {}] has_next: Fetched, buffer still empty, PULL confirms no more.", (void*)this);
            stream_fully_consumed_or_discarded_ = true;
        }
        if (logger) logger->trace("[ResultStream {}] has_next: After fetch, out_has_next={}", (void*)this, out_has_next);
        return {boltprotocol::BoltError::SUCCESS, ""};
    }

    std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>> BoltResultStream::next() {
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        bool has_more = false;
        auto has_next_res = has_next(has_more);

        if (has_next_res.first != boltprotocol::BoltError::SUCCESS) {
            return {has_next_res.first, has_next_res.second, std::nullopt};
        }
        if (!has_more) {
            if (stream_failed_) return {failure_reason_, failure_message_, std::nullopt};
            if (logger) logger->trace("[ResultStream {}] next: No more records.", (void*)this);
            // This is not an error, just end of stream
            return {boltprotocol::BoltError::SUCCESS, "No more records in stream.", std::nullopt};
        }

        // This check should be redundant if has_next() is correct
        if (raw_record_buffer_.empty() && !stream_failed_) {
            _set_failure_state(boltprotocol::BoltError::UNKNOWN_ERROR, "Internal error: has_next() was true but buffer is empty and not failed.");
            if (logger) logger->error("[ResultStream {}] next: Internal error - has_next true but buffer empty.", (void*)this);
            return {failure_reason_, failure_message_, std::nullopt};
        }
        // This check is important if has_next() set stream_failed_ during its fetch attempt
        if (stream_failed_) return {failure_reason_, failure_message_, std::nullopt};

        boltprotocol::RecordMessageParams raw_record_params = std::move(raw_record_buffer_.front());
        raw_record_buffer_.pop_front();

        if (logger) logger->trace("[ResultStream {}] next: Popped one record. Buffer size: {}", (void*)this, raw_record_buffer_.size());

        // Construct BoltRecord by moving fields_data
        BoltRecord record(std::move(raw_record_params.fields), field_names_ptr_cache_);
        // Construct optional by moving the BoltRecord
        return {boltprotocol::BoltError::SUCCESS, "", std::make_optional<BoltRecord>(std::move(record))};
    }

    std::tuple<boltprotocol::BoltError, std::string, std::vector<BoltRecord>> BoltResultStream::list_all() {
        std::vector<BoltRecord> all_records_converted;
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        if (logger) logger->trace("[ResultStream {}] list_all: Starting.", (void*)this);

        if (stream_failed_) {
            if (logger) logger->trace("[ResultStream {}] list_all: Stream already failed.", (void*)this);
            return {failure_reason_, failure_message_, std::move(all_records_converted)};
        }

        while (true) {
            auto next_res_tuple = next();  // next() handles fetching logic
            boltprotocol::BoltError err_code = std::get<0>(next_res_tuple);
            std::string& err_msg = std::get<1>(next_res_tuple);                             // Use ref to avoid copy
            std::optional<BoltRecord> record_opt = std::get<2>(std::move(next_res_tuple));  // Move the optional

            if (err_code != boltprotocol::BoltError::SUCCESS) {
                if (logger) logger->warn("[ResultStream {}] list_all: Error from next(): {}.", (void*)this, err_msg);
                return {err_code, std::move(err_msg), std::move(all_records_converted)};
            }
            if (!record_opt.has_value()) {  // End of stream
                if (logger) logger->trace("[ResultStream {}] list_all: End of stream reached by next().", (void*)this);
                break;
            }
            all_records_converted.push_back(std::move(*record_opt));  // Move the BoltRecord from optional
        }

        if (logger) logger->trace("[ResultStream {}] list_all: Finished. Records: {}", (void*)this, all_records_converted.size());
        return {boltprotocol::BoltError::SUCCESS, "", std::move(all_records_converted)};
    }

}  // namespace neo4j_bolt_transport