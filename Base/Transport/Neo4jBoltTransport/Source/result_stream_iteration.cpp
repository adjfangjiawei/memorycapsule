#include <iostream>
#include <utility>

#include "neo4j_bolt_transport/result_stream.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    // ... (has_next 实现) ...
    std::pair<boltprotocol::BoltError, std::string> BoltResultStream::has_next(bool& out_has_next) {
        out_has_next = false;
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;

        if (stream_failed_) {
            if (logger) logger->trace("[ResultStreamITER {}] has_next: Stream already failed. Reason: {}", (void*)this, static_cast<int>(failure_reason_));
            return {failure_reason_, failure_message_};
        }
        if (stream_fully_consumed_or_discarded_) {
            if (logger) logger->trace("[ResultStreamITER {}] has_next: Stream fully consumed/discarded.", (void*)this);
            return {boltprotocol::BoltError::SUCCESS, ""};
        }
        if (!raw_record_buffer_.empty()) {
            if (logger) logger->trace("[ResultStreamITER {}] has_next: Records in buffer.", (void*)this);
            out_has_next = true;
            return {boltprotocol::BoltError::SUCCESS, ""};
        }

        bool effectively_has_more_on_server = is_first_pull_attempt_ ? initial_server_has_more_records_ : server_has_more_records_;

        if (!effectively_has_more_on_server) {
            if (logger) logger->trace("[ResultStreamITER {}] has_next: Buffer empty, server indicates no more records.", (void*)this);
            stream_fully_consumed_or_discarded_ = true;
            // 如果这是流的末尾，并且没有发生错误，确保 final_summary_typed_ 是最新的
            // （它可能已经是 run_summary_typed_ 或者上一次 PULL 的结果）
            // 如果之前没有 PULL/DISCARD，并且 RUN 表明没有更多记录，final_summary 应该等于 run_summary
            if (is_first_pull_attempt_) {
                // 确保 final_summary_typed_ 反映的是 run_summary_typed_ 的状态
                // 在 BoltResultStream 构造函数中，final_summary_typed_ 已经用 run_summary_params_raw 初始化
            }
            return {boltprotocol::BoltError::SUCCESS, ""};
        }

        int64_t fetch_n = 1000;  // 默认的拉取大小
        if (owner_session_ && owner_session_->session_params_.default_fetch_size != 0) {
            fetch_n = (owner_session_->session_params_.default_fetch_size > 0 || owner_session_->session_params_.default_fetch_size == -1) ? owner_session_->session_params_.default_fetch_size : 1000;
        }

        if (logger) logger->trace("[ResultStreamITER {}] has_next: Buffer empty, attempting to fetch {} records.", (void*)this, fetch_n);
        auto fetch_result = _fetch_more_records(fetch_n);  // 这个方法内部会更新 final_summary_typed_

        if (fetch_result.first != boltprotocol::BoltError::SUCCESS) {
            return fetch_result;  // _fetch_more_records 内部已设置失败状态
        }

        out_has_next = !raw_record_buffer_.empty();
        if (!out_has_next && !server_has_more_records_) {  // 拉取后，缓冲区仍为空，且服务器确认没有更多
            if (logger) logger->trace("[ResultStreamITER {}] has_next: Fetched, buffer still empty, PULL confirms no more.", (void*)this);
            stream_fully_consumed_or_discarded_ = true;
        }
        if (logger) logger->trace("[ResultStreamITER {}] has_next: After fetch, out_has_next={}", (void*)this, out_has_next);
        return {boltprotocol::BoltError::SUCCESS, ""};
    }

    std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>> BoltResultStream::next() {
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        bool has_more = false;
        auto has_next_res_pair = has_next(has_more);  // has_next_res_pair 是 std::pair

        boltprotocol::BoltError err_code_has_next = has_next_res_pair.first;
        std::string err_msg_has_next = std::move(has_next_res_pair.second);  // 从 pair 中移动

        if (err_code_has_next != boltprotocol::BoltError::SUCCESS) {
            return {err_code_has_next, std::move(err_msg_has_next), std::nullopt};
        }
        if (!has_more) {
            if (stream_failed_) return {failure_reason_, failure_message_, std::nullopt};
            if (logger) logger->trace("[ResultStreamITER {}] next: No more records.", (void*)this);
            return {boltprotocol::BoltError::SUCCESS, "No more records in stream.", std::nullopt};
        }

        if (raw_record_buffer_.empty() && !stream_failed_) {
            _set_failure_state(boltprotocol::BoltError::UNKNOWN_ERROR, "Internal error: has_next() was true but buffer is empty and not failed.");
            if (logger) logger->error("[ResultStreamITER {}] next: Internal error - has_next true but buffer empty.", (void*)this);
            return {failure_reason_, failure_message_, std::nullopt};
        }
        if (stream_failed_) return {failure_reason_, failure_message_, std::nullopt};

        boltprotocol::RecordMessageParams raw_record_params = std::move(raw_record_buffer_.front());
        raw_record_buffer_.pop_front();

        if (logger) logger->trace("[ResultStreamITER {}] next: Popped one record. Buffer size: {}", (void*)this, raw_record_buffer_.size());

        BoltRecord record(std::move(raw_record_params.fields), field_names_ptr_cache_);
        return {boltprotocol::BoltError::SUCCESS, "", std::make_optional<BoltRecord>(std::move(record))};
    }

    std::tuple<boltprotocol::BoltError, std::string, std::optional<BoltRecord>> BoltResultStream::single() {
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        if (logger) logger->trace("[ResultStreamITER {}] single() called.", (void*)this);

        auto next_result_tuple = next();  // 调用 next()
        boltprotocol::BoltError err_code_next = std::get<0>(next_result_tuple);
        std::string err_msg_next = std::get<1>(std::move(next_result_tuple));
        std::optional<BoltRecord> record_opt = std::get<2>(std::move(next_result_tuple));

        if (err_code_next != boltprotocol::BoltError::SUCCESS) {
            if (logger) logger->warn("[ResultStreamITER {}] single(): Error from first next(): {}.", (void*)this, err_msg_next);
            return {err_code_next, std::move(err_msg_next), std::nullopt};
        }
        if (!record_opt.has_value()) {
            if (stream_failed_) {  // 如果流因为 next() 内部的 has_next() -> _fetch_more_records() 失败
                if (logger) logger->trace("[ResultStreamITER {}] single(): No record, stream failed. Reason: {}.", (void*)this, failure_message_);
                return {failure_reason_, failure_message_, std::nullopt};
            }
            // 如果没有失败，但 next() 返回空，意味着流是空的
            _set_failure_state(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Expected a single record, but stream was empty.");
            if (logger) logger->warn("[ResultStreamITER {}] single(): Expected single record, stream empty.", (void*)this);
            return {failure_reason_, failure_message_, std::nullopt};
        }

        // 成功获取一个记录，现在检查是否还有更多
        bool has_more_records = false;
        auto has_next_res_pair = has_next(has_more_records);
        boltprotocol::BoltError err_code_has_next = has_next_res_pair.first;
        std::string err_msg_has_next = std::move(has_next_res_pair.second);

        if (err_code_has_next != boltprotocol::BoltError::SUCCESS) {
            if (logger) logger->warn("[ResultStreamITER {}] single(): Error checking for more records after finding one: {}.", (void*)this, err_msg_has_next);
            _set_failure_state(err_code_has_next, "Error checking for subsequent records in single(): " + err_msg_has_next);
            return {failure_reason_, failure_message_, std::nullopt};
        }

        if (has_more_records) {
            if (logger) logger->warn("[ResultStreamITER {}] single(): Expected single record, but more found. Discarding rest.", (void*)this);
            auto discard_res = _discard_all_remaining_records();
            if (discard_res.first != boltprotocol::BoltError::SUCCESS && logger) {
                logger->warn("[ResultStreamITER {}] single(): Discarding extra records failed: {}", (void*)this, discard_res.second);
            }
            _set_failure_state(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, "Expected a single record, but found more.");
            return {failure_reason_, failure_message_, std::nullopt};
        }

        if (logger) logger->trace("[ResultStreamITER {}] single() successful.", (void*)this);
        return {boltprotocol::BoltError::SUCCESS, "", std::move(record_opt)};
    }

    std::tuple<boltprotocol::BoltError, std::string, std::vector<BoltRecord>> BoltResultStream::list_all() {
        std::vector<BoltRecord> all_records_converted;
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        if (logger) logger->trace("[ResultStreamITER {}] list_all: Starting.", (void*)this);

        if (stream_failed_) {
            if (logger) logger->trace("[ResultStreamITER {}] list_all: Stream already failed.", (void*)this);
            return {failure_reason_, failure_message_, std::move(all_records_converted)};
        }

        while (true) {
            auto next_res_tuple = next();
            boltprotocol::BoltError err_code = std::get<0>(next_res_tuple);
            std::string err_msg = std::get<1>(std::move(next_res_tuple));                   // 可以移动
            std::optional<BoltRecord> record_opt = std::get<2>(std::move(next_res_tuple));  // 可以移动

            if (err_code != boltprotocol::BoltError::SUCCESS) {
                if (logger) logger->warn("[ResultStreamITER {}] list_all: Error from next(): {}.", (void*)this, err_msg);
                // 返回已收集的记录以及错误
                return {err_code, std::move(err_msg), std::move(all_records_converted)};
            }
            if (!record_opt.has_value()) {  // 流结束
                if (logger) logger->trace("[ResultStreamITER {}] list_all: End of stream reached by next().", (void*)this);
                break;
            }
            all_records_converted.push_back(std::move(*record_opt));  // 从 optional 中移动 BoltRecord
        }

        if (logger) logger->trace("[ResultStreamITER {}] list_all: Finished. Records: {}", (void*)this, all_records_converted.size());
        return {boltprotocol::BoltError::SUCCESS, "", std::move(all_records_converted)};
    }

}  // namespace neo4j_bolt_transport