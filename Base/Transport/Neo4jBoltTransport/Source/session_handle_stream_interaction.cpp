#include <iostream>  // 调试用
#include <utility>   // For std::move

#include "boltprotocol/message_serialization.h"  // For serialize_..._message
#include "boltprotocol/packstream_writer.h"      // For PackStreamWriter
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    // 已重命名为 _prepare_auto_commit_run
    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_prepare_auto_commit_run(const std::string& cypher,
                                                                                            const std::map<std::string, boltprotocol::Value>& parameters,
                                                                                            const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata,
                                                                                            boltprotocol::SuccessMessageParams& out_run_summary_raw,        // 输出原始摘要
                                                                                            boltprotocol::FailureMessageParams& out_failure_details_raw) {  // 输出原始失败详情

        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_prepare_auto_commit_run");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        boltprotocol::RunMessageParams run_p;
        run_p.cypher_query = cypher;
        run_p.parameters = parameters;
        run_p.bookmarks = current_bookmarks_;  // 发送当前书签
        if (session_params_.database_name.has_value()) run_p.db = session_params_.database_name;
        if (session_params_.impersonated_user.has_value()) run_p.imp_user = session_params_.impersonated_user;

        // 对于 Bolt < 5.0, 访问模式是 RUN 的一部分。对于 >= 5.0, 它是 BEGIN 的一部分。
        // 对于自动提交，我们可以在 RUN 中设置它（如果适用）。
        if (conn->get_bolt_version() < boltprotocol::versions::Version(5, 0)) {
            if (session_params_.default_access_mode == config::AccessMode::READ) run_p.mode = "r";
        }
        if (tx_metadata.has_value()) run_p.tx_metadata = *tx_metadata;

        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_writer(run_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_run_message(run_p, run_writer, conn->get_bolt_version());
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Auto-commit RUN serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);  // 标记会话连接无效
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending auto-commit RUN (no immediate PULL/DISCARD).", conn->get_id());
        // send_request_receive_summary 获取 RUN 消息的直接 SUCCESS/FAILURE 响应
        err = conn->send_request_receive_summary(run_payload_bytes, out_run_summary_raw, out_failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {  // RUN 消息的 IO 或协议错误
            std::string msg = error::format_error_message("Auto-commit RUN send/receive summary", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }
        // 检查服务器是否为 RUN 消息返回了 FAILURE
        if (conn->get_last_error_code() != boltprotocol::BoltError::SUCCESS) {
            std::string server_fail_detail = error::format_server_failure(out_failure_details_raw);
            std::string msg = error::format_error_message("Auto-commit RUN server failure", conn->get_last_error_code(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }

        // RUN 成功。书签更新通常在 PULL/DISCARD 之后，但如果 RUN 消息本身返回书签（Bolt 4.0+ 可能），
        // ResultStream 的 consume 方法会处理。这里不处理书签。

        if (logger) logger->trace("[SessionStream {}] Auto-commit RUN successful, got its summary.", conn->get_id());
        return {boltprotocol::BoltError::SUCCESS, ""};
    }

    // 已重命名为 _prepare_explicit_tx_run
    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_prepare_explicit_tx_run(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, boltprotocol::SuccessMessageParams& out_run_summary_raw, boltprotocol::FailureMessageParams& out_failure_details_raw) {
        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_prepare_explicit_tx_run");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        if (!in_explicit_transaction_) {  // 应该由调用者检查，但作为防御
            if (logger) logger->warn("[SessionStream {}] _prepare_explicit_tx_run called when not in transaction.", conn->get_id());
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Cannot run query in explicit TX mode; not in transaction."};
        }

        boltprotocol::RunMessageParams run_p;
        run_p.cypher_query = cypher;
        run_p.parameters = parameters;
        // 在显式事务中，RUN 消息不包含书签、事务元数据、模式、数据库或模拟用户。这些在 BEGIN 时设置。

        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_w(run_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_run_message(run_p, run_w, conn->get_bolt_version());
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Explicit TX RUN serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending explicit TX RUN.", conn->get_id());
        err = conn->send_request_receive_summary(run_payload_bytes, out_run_summary_raw, out_failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {  // RUN 消息的 IO 或协议错误
            std::string msg = error::format_error_message("Explicit TX RUN send/receive summary", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {  // 服务器为 RUN 消息返回了 SUCCESS
            current_transaction_query_id_.reset();                              // 先重置
            // 对于 Bolt >= 4.0, RUN 在显式事务中返回一个 qid。对于 < 4.0, 它不返回。
            // Bolt < 4.0 中的 PULL/DISCARD 隐式使用 qid = -1 (代表最后一次RUN)。
            // Bolt >= 4.0 中的 PULL/DISCARD 需要 RUN 返回的 qid。
            if (!(conn->get_bolt_version() < boltprotocol::versions::Version(4, 0))) {
                auto it_qid = out_run_summary_raw.metadata.find("qid");
                if (it_qid != out_run_summary_raw.metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
                    current_transaction_query_id_ = std::get<int64_t>(it_qid->second);
                    if (logger) logger->trace("[SessionStream {}] Explicit TX RUN successful, qid: {}.", conn->get_id(), *current_transaction_query_id_);
                } else {
                    // 对于 Bolt >= 4.0，如果 qid 缺失，则是一个错误
                    std::string msg = "Missing qid in RUN SUCCESS for explicit transaction (Bolt >= 4.0).";
                    _invalidate_session_due_to_connection_error(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg);
                    return {boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg};
                }
            } else {  // Bolt < 4.0
                if (logger) logger->trace("[SessionStream {}] Explicit TX RUN successful (Bolt < 4.0, no qid expected from RUN).", conn->get_id());
            }
            return {boltprotocol::BoltError::SUCCESS, ""};
        } else {  // 服务器为 RUN 消息返回了 FAILURE
            std::string server_fail_detail = error::format_server_failure(out_failure_details_raw);
            std::string msg = error::format_error_message("Explicit TX RUN server failure", conn->get_last_error_code(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }
    }

    // _stream_pull_records: 与 BoltPhysicalConnection 交互以发送 PULL 并接收记录和摘要
    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_stream_pull_records(std::optional<int64_t> qid, int64_t n, std::vector<boltprotocol::RecordMessageParams>& out_records,
                                                                                        boltprotocol::SuccessMessageParams& out_pull_summary_raw) {  // 输出原始摘要

        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_stream_pull_records");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        boltprotocol::PullMessageParams pull_p;
        pull_p.n = n;
        pull_p.qid = qid;  // 对于 Bolt < 4.0 的显式事务，这可能是 nullopt (服务器隐式使用 qid=-1)
                           // 对于 Bolt >= 4.0 的显式事务，这应该是 RUN 返回的 qid
                           // 对于自动提交，这通常是 nullopt (服务器隐式使用 qid=-1 或 RUN 返回的 qid)

        std::vector<uint8_t> pull_payload_bytes;
        boltprotocol::PackStreamWriter writer(pull_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_pull_message(pull_p, writer);
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("PULL serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending PULL (n={}, qid={}).", conn->get_id(), n, qid.has_value() ? std::to_string(qid.value()) : "none");

        boltprotocol::FailureMessageParams failure_details_raw;  // 用于捕获 PULL 消息的 FAILURE 响应

        // 定义记录处理器 lambda
        auto record_processor = [&](boltprotocol::MessageTag /*tag*/, const std::vector<uint8_t>& rec_payload, internal::BoltPhysicalConnection& /*connection_ref*/) {
            boltprotocol::RecordMessageParams rec;
            boltprotocol::PackStreamReader r(rec_payload);  // 用 payload 构造 reader
            if (boltprotocol::deserialize_record_message(r, rec) == boltprotocol::BoltError::SUCCESS) {
                out_records.push_back(std::move(rec));
                return boltprotocol::BoltError::SUCCESS;
            }
            if (logger) logger->error("[SessionStream {}] Failed to deserialize RECORD message.", conn->get_id());
            return boltprotocol::BoltError::DESERIALIZATION_ERROR;
        };

        // send_request_receive_stream 会处理 RECORD 流和最后的 SUCCESS/FAILURE
        err = conn->send_request_receive_stream(pull_payload_bytes, record_processor, out_pull_summary_raw, failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {  // 流处理或最终摘要的 IO/协议错误
            std::string msg = error::format_error_message("PULL stream processing", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        // 检查服务器是否为 PULL 操作本身返回了 FAILURE (这在 send_request_receive_stream 内部被转换为 conn->get_last_error_code())
        if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
            // PULL 成功，并且收到了 SUCCESS 摘要
            // 如果不是在显式事务中，则从 PULL 的 SUCCESS 摘要更新书签
            if (!is_in_transaction()) {
                auto it_bookmark = out_pull_summary_raw.metadata.find("bookmark");
                if (it_bookmark != out_pull_summary_raw.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                    update_bookmarks({std::get<std::string>(it_bookmark->second)});
                } else {
                    update_bookmarks({});  // 如果没有书签，则清除
                }
            }
            if (logger) {
                bool has_more = false;
                auto it_has_more = out_pull_summary_raw.metadata.find("has_more");
                if (it_has_more != out_pull_summary_raw.metadata.end() && std::holds_alternative<bool>(it_has_more->second)) {
                    has_more = std::get<bool>(it_has_more->second);
                }
                logger->trace("[SessionStream {}] PULL successful. Records received: {}. HasMore: {}", conn->get_id(), out_records.size(), has_more);
            }
            return {boltprotocol::BoltError::SUCCESS, ""};
        } else {  // 服务器为 PULL 请求返回了 FAILURE
            std::string server_fail_detail = error::format_server_failure(failure_details_raw);
            std::string msg = error::format_error_message("PULL server failure", conn->get_last_error_code(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }
    }

    // _stream_discard_records: 与 BoltPhysicalConnection 交互以发送 DISCARD 并接收摘要
    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_stream_discard_records(std::optional<int64_t> qid, int64_t n,
                                                                                           boltprotocol::SuccessMessageParams& out_discard_summary_raw) {  // 输出原始摘要

        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_stream_discard_records");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        boltprotocol::DiscardMessageParams discard_p;
        discard_p.n = n;  // 通常为 -1 以丢弃所有
        discard_p.qid = qid;

        std::vector<uint8_t> discard_payload_bytes;
        boltprotocol::PackStreamWriter writer(discard_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_discard_message(discard_p, writer);
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("DISCARD serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending DISCARD (n={}, qid={}).", conn->get_id(), n, qid.has_value() ? std::to_string(qid.value()) : "none");

        boltprotocol::FailureMessageParams failure_details_raw;
        err = conn->send_request_receive_summary(discard_payload_bytes, out_discard_summary_raw, failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {  // DISCARD 消息的 IO 或协议错误
            std::string msg = error::format_error_message("DISCARD send/receive summary", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {  // 服务器为 DISCARD 消息返回了 SUCCESS
            // 如果不是在显式事务中，则从 DISCARD 的 SUCCESS 摘要更新书签
            if (!is_in_transaction()) {
                auto it_bookmark = out_discard_summary_raw.metadata.find("bookmark");
                if (it_bookmark != out_discard_summary_raw.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                    update_bookmarks({std::get<std::string>(it_bookmark->second)});
                } else {
                    update_bookmarks({});
                }
            }
            if (logger) logger->trace("[SessionStream {}] DISCARD successful.", conn->get_id());
            return {boltprotocol::BoltError::SUCCESS, ""};
        } else {  // 服务器为 DISCARD 消息返回了 FAILURE
            std::string server_fail_detail = error::format_server_failure(failure_details_raw);
            std::string msg = error::format_error_message("DISCARD server failure", conn->get_last_error_code(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }
    }

}  // namespace neo4j_bolt_transport