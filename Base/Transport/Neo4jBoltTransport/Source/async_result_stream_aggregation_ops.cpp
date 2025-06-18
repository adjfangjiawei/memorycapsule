#include <optional>  // For std::optional in single_async
#include <tuple>     // For std::tuple
#include <utility>   // For std::move
#include <vector>    // For std::vector in list_all_async

#include "neo4j_bolt_transport/async_result_stream.h"
#include "neo4j_bolt_transport/async_session_handle.h"    // For logger access via owner_session_
#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // For error formatting
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"    // For logger access via transport_manager_

namespace neo4j_bolt_transport {

    // --- AsyncResultStream Public Method: single_async ---
    boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>>> AsyncResultStream::single_async() {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
            logger = owner_session_->transport_manager_->get_config().logger;
        }
        if (logger) logger->trace("[AsyncResultStream {}] single_async called.", (void*)this);

        auto [err_code_first, err_msg_first, record_opt_first] = co_await next_async();

        if (err_code_first != boltprotocol::BoltError::SUCCESS) {
            if (logger) logger->warn("[AsyncResultStream {}] single_async: Error fetching first record: {}", (void*)this, err_msg_first);
            co_return std::make_tuple(err_code_first, std::move(err_msg_first), std::nullopt);
        }
        if (!record_opt_first.has_value()) {
            std::string msg = "Expected a single record, but the stream was empty.";
            set_failure_state(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg);
            if (logger) logger->warn("[AsyncResultStream {}] single_async: {}", (void*)this, msg);
            co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, std::nullopt);
        }

        // Successfully fetched one record. Now check if there are more.
        auto [err_code_second, err_msg_second, record_opt_second] = co_await next_async();

        if (err_code_second != boltprotocol::BoltError::SUCCESS) {
            std::string msg = "Error checking for subsequent records after fetching one in single_async: " + err_msg_second;
            set_failure_state(err_code_second, msg);
            if (logger) logger->warn("[AsyncResultStream {}] single_async: {}", (void*)this, msg);
            co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, std::nullopt);
        }

        if (record_opt_second.has_value()) {
            std::string msg = "Expected a single record, but more were found in the stream.";
            set_failure_state(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg);
            if (logger) logger->warn("[AsyncResultStream {}] single_async: {}", (void*)this, msg);
            co_await consume_async();
            co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, std::nullopt);
        }

        if (logger) logger->trace("[AsyncResultStream {}] single_async successful.", (void*)this);
        stream_fully_consumed_or_discarded_.store(true, std::memory_order_release);
        co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", std::move(record_opt_first));
    }

    // --- AsyncResultStream Public Method: list_all_async ---
    boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, std::vector<BoltRecord>>> AsyncResultStream::list_all_async() {
        std::vector<BoltRecord> all_records;
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (owner_session_ && owner_session_->transport_manager_ && owner_session_->transport_manager_->get_config().logger) {
            logger = owner_session_->transport_manager_->get_config().logger;
        }
        if (logger) logger->trace("[AsyncResultStream {}] list_all_async called.", (void*)this);

        if (stream_failed_.load(std::memory_order_acquire)) {
            co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, std::move(all_records));
        }
        if (stream_fully_consumed_or_discarded_.load(std::memory_order_acquire) && raw_record_buffer_.empty()) {
            co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", std::move(all_records));
        }

        while (true) {
            auto [err_code, err_msg, record_opt] = co_await next_async();
            if (err_code != boltprotocol::BoltError::SUCCESS) {
                if (logger) logger->warn("[AsyncResultStream {}] list_all_async: Error during iteration: {}", (void*)this, err_msg);
                co_return std::make_tuple(err_code, std::move(err_msg), std::move(all_records));
            }
            if (!record_opt.has_value()) {
                break;
            }
            try {
                all_records.push_back(std::move(*record_opt));
            } catch (const std::bad_alloc&) {
                set_failure_state(boltprotocol::BoltError::OUT_OF_MEMORY, "Out of memory while collecting records in list_all_async.");
                co_return std::make_tuple(failure_reason_.load(std::memory_order_acquire), failure_message_, std::move(all_records));
            }
        }
        if (logger) logger->trace("[AsyncResultStream {}] list_all_async successful. Collected {} records.", (void*)this, all_records.size());
        co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", std::move(all_records));
    }

}  // namespace neo4j_bolt_transport