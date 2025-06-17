#include <iostream>  // 调试用
#include <utility>   // For std::move

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/result_stream.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    std::pair<boltprotocol::BoltError, std::string> BoltResultStream::_fetch_more_records(int64_t n) {
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;

        if (!owner_session_ || !owner_session_->is_connection_valid()) {
            std::string msg = "Fetch records: Invalid session or connection.";
            _set_failure_state(boltprotocol::BoltError::NETWORK_ERROR, msg);
            if (logger) logger->warn("[ResultStreamFETCH {}] {}", (void*)this, msg);
            return {failure_reason_, failure_message_};
        }
        if (stream_failed_ || (stream_fully_consumed_or_discarded_ && !is_first_pull_attempt_)) {
            if (logger) logger->trace("[ResultStreamFETCH {}] Already failed or consumed. Failed: {}, Consumed: {}, FirstPull: {}", (void*)this, stream_failed_, stream_fully_consumed_or_discarded_, is_first_pull_attempt_);
            return {failure_reason_ != boltprotocol::BoltError::SUCCESS ? failure_reason_ : boltprotocol::BoltError::UNKNOWN_ERROR, failure_message_};
        }

        if (logger) logger->trace("[ResultStreamFETCH {}] Fetching {} records. QID: {}", (void*)this, n, (query_id_ ? std::to_string(*query_id_) : "auto"));

        std::vector<boltprotocol::RecordMessageParams> fetched_records;
        boltprotocol::SuccessMessageParams current_pull_summary_raw;  // 从 _stream_pull_records 获取原始摘要

        std::optional<int64_t> qid_for_this_pull = query_id_;

        // 调用 SessionHandle 的方法来实际与物理连接交互
        auto pull_result_pair = owner_session_->_stream_pull_records(qid_for_this_pull, n, fetched_records, current_pull_summary_raw);
        is_first_pull_attempt_ = false;  // 无论成功与否，都已经尝试过 PULL

        if (pull_result_pair.first != boltprotocol::BoltError::SUCCESS) {
            std::optional<boltprotocol::FailureMessageParams> fail_details;
            // 尝试从连接获取更具体的服务器错误信息（如果与 PULL 操作的错误不同）
            if (owner_session_ && owner_session_->connection_ && owner_session_->connection_->get_last_error_code() != boltprotocol::BoltError::SUCCESS) {
                if (owner_session_->connection_->get_last_error_code() != pull_result_pair.first) {  // 仅当连接错误更具体时
                    boltprotocol::FailureMessageParams temp_fail;
                    temp_fail.metadata["message"] = boltprotocol::Value(owner_session_->connection_->get_last_error_message());
                    // temp_fail.metadata["code"] = ... (如果可以从 BoltPhysicalConnection 获取 Neo4j 错误码)
                    fail_details = temp_fail;
                }
            }
            _set_failure_state(pull_result_pair.first, pull_result_pair.second, fail_details);
            if (logger) logger->warn("[ResultStreamFETCH {}] _stream_pull_records failed. Error: {}, Msg: {}", (void*)this, static_cast<int>(pull_result_pair.first), pull_result_pair.second);
            return {failure_reason_, failure_message_};
        }

        // PULL 消息交换成功，服务器返回了 SUCCESS 摘要
        _update_final_summary(std::move(current_pull_summary_raw));  // 使用收到的原始摘要更新类型化的 final_summary_typed_

        for (auto& rec : fetched_records) {
            raw_record_buffer_.push_back(std::move(rec));
        }

        // 从更新后的 final_summary_typed_ 中检查 has_more
        auto it_has_more = final_summary_typed_.raw_params().metadata.find("has_more");
        if (it_has_more != final_summary_typed_.raw_params().metadata.end() && std::holds_alternative<bool>(it_has_more->second)) {
            server_has_more_records_ = std::get<bool>(it_has_more->second);
        } else {
            server_has_more_records_ = false;  // 如果 PULL 摘要中没有 "has_more"，则假定没有更多了
        }

        if (!server_has_more_records_ && raw_record_buffer_.empty()) {
            stream_fully_consumed_or_discarded_ = true;
        }
        if (logger) logger->trace("[ResultStreamFETCH {}] Fetched {}. Buffer: {}. ServerMore: {}", (void*)this, fetched_records.size(), raw_record_buffer_.size(), server_has_more_records_);
        return {boltprotocol::BoltError::SUCCESS, ""};
    }

    std::pair<boltprotocol::BoltError, std::string> BoltResultStream::_discard_all_remaining_records() {
        std::shared_ptr<spdlog::logger> logger = (owner_session_ && owner_session_->connection_) ? owner_session_->connection_->get_logger() : nullptr;
        if (logger) logger->trace("[ResultStreamDISCARD {}] Discarding. QID: {}", (void*)this, (query_id_ ? std::to_string(*query_id_) : "auto"));

        if (!owner_session_ || !owner_session_->is_connection_valid()) {
            _set_failure_state(boltprotocol::BoltError::NETWORK_ERROR, "Discard: Invalid session/connection.");
            if (logger) logger->warn("[ResultStreamDISCARD {}] Invalid session/connection.", (void*)this);
            return {failure_reason_, failure_message_};
        }
        if (stream_failed_ || stream_fully_consumed_or_discarded_) {
            if (logger) logger->trace("[ResultStreamDISCARD {}] Already failed or consumed. Failed: {}, Consumed: {}", (void*)this, stream_failed_, stream_fully_consumed_or_discarded_);
            return {failure_reason_ != boltprotocol::BoltError::SUCCESS ? failure_reason_ : boltprotocol::BoltError::SUCCESS, failure_message_};
        }

        raw_record_buffer_.clear();  // 清空本地缓冲的记录

        // 检查是否真的需要向服务器发送 DISCARD
        // server_has_more_records_ 反映了上一个 PULL/DISCARD 的 has_more 标志
        // initial_server_has_more_records_ 反映了 RUN 响应的 has_more 标志
        // is_first_pull_attempt_ 表示是否还没有执行过 PULL 或 DISCARD
        bool needs_server_discard = false;
        if (is_first_pull_attempt_) {  // 第一次操作（之前没有PULL/DISCARD）
            needs_server_discard = initial_server_has_more_records_;
        } else {  // 已经有过PULL/DISCARD
            needs_server_discard = server_has_more_records_;
        }

        if (!needs_server_discard) {
            stream_fully_consumed_or_discarded_ = true;
            // 如果这是第一次操作（例如，RUN 后直接 consume，且 RUN 表明没有记录），
            // final_summary_typed_ 应该等于 run_summary_typed_。
            // _update_final_summary 在构造时已经用 run_summary 初始化了 final_summary。
            // 如果之前有PULL，final_summary_typed_ 已经被该PULL的摘要更新。
            if (is_first_pull_attempt_) {
                // 确保 final_summary_typed_ 反映的是 run_summary_typed_ 的状态，因为它没有发生网络交互
                // _update_final_summary(boltprotocol::SuccessMessageParams(run_summary_typed_.raw_params()));
                // 上面这行可能不必要，因为构造时 final_summary_typed_ 已经是 run_summary_typed_ 的一个副本了。
            }
            if (logger) logger->trace("[ResultStreamDISCARD {}] No records on server to discard. FirstPull: {}, InitialServerMore: {}, CurrentServerMore: {}", (void*)this, is_first_pull_attempt_, initial_server_has_more_records_, server_has_more_records_);
            return {boltprotocol::BoltError::SUCCESS, ""};
        }

        // 需要向服务器发送 DISCARD
        boltprotocol::SuccessMessageParams discard_summary_raw;  // 用于接收原始摘要
        std::optional<int64_t> qid_for_discard = query_id_;

        auto discard_result_pair = owner_session_->_stream_discard_records(qid_for_discard, -1, discard_summary_raw);
        is_first_pull_attempt_ = false;              // 标记已尝试过 PULL/DISCARD
        stream_fully_consumed_or_discarded_ = true;  // DISCARD 意味着流结束

        if (discard_result_pair.first != boltprotocol::BoltError::SUCCESS) {
            std::optional<boltprotocol::FailureMessageParams> fail_details;
            if (owner_session_ && owner_session_->connection_ && owner_session_->connection_->get_last_error_code() != boltprotocol::BoltError::SUCCESS) {
                if (owner_session_->connection_->get_last_error_code() != discard_result_pair.first) {
                    boltprotocol::FailureMessageParams temp_fail;
                    temp_fail.metadata["message"] = boltprotocol::Value(owner_session_->connection_->get_last_error_message());
                    fail_details = temp_fail;
                }
            }
            _set_failure_state(discard_result_pair.first, discard_result_pair.second, fail_details);
            if (logger) logger->warn("[ResultStreamDISCARD {}] _stream_discard_records failed. Error: {}, Msg: {}", (void*)this, static_cast<int>(discard_result_pair.first), discard_result_pair.second);
            return {failure_reason_, failure_message_};
        }

        _update_final_summary(std::move(discard_summary_raw));  // 使用收到的原始摘要更新类型化的 final_summary_typed_
        server_has_more_records_ = false;                       // DISCARD 后，服务器肯定没有更多记录了
        if (logger) logger->trace("[ResultStreamDISCARD {}] Discard successful.", (void*)this);
        return {boltprotocol::BoltError::SUCCESS, ""};
    }

}  // namespace neo4j_bolt_transport