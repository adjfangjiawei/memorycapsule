#include <optional>  // For std::optional
#include <string>    // For std::string
#include <tuple>     // For std::tuple
#include <utility>   // For std::move

#include "neo4j_bolt_transport/async_result_stream.h"
#include "neo4j_bolt_transport/bolt_record.h"  // For BoltRecord

namespace neo4j_bolt_transport {

    // --- AsyncResultStream Public Method: next_async ---
    boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>>> AsyncResultStream::next_async() {
        // ensure_records_buffered_async will handle logging, failure state, etc.
        auto [err_code, err_msg, has_more_locally] = co_await ensure_records_buffered_async();

        if (err_code != boltprotocol::BoltError::SUCCESS) {
            // If ensure_records_buffered_async failed, it already set the failure state
            co_return std::make_tuple(err_code, std::move(err_msg), std::nullopt);
        }
        if (!has_more_locally) {
            // Stream is exhausted, no error, just no more records
            co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "No more records in stream.", std::nullopt);
        }

        // Buffer has records if has_more_locally is true
        boltprotocol::RecordMessageParams raw_record_params = std::move(raw_record_buffer_.front());
        raw_record_buffer_.pop_front();

        BoltRecord record(std::move(raw_record_params.fields), field_names_ptr_cache_);
        co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", std::make_optional(std::move(record)));
    }

}  // namespace neo4j_bolt_transport