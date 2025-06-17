// Source/session_handle_stream_interaction.cpp
#include <iostream>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/result_stream.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_prepare_auto_commit_stream(
        const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, boltprotocol::SuccessMessageParams& out_run_summary, boltprotocol::FailureMessageParams& out_failure) {
        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_prepare_auto_commit_stream");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        boltprotocol::RunMessageParams run_p;
        run_p.cypher_query = cypher;
        run_p.parameters = parameters;
        run_p.bookmarks = current_bookmarks_;
        if (session_params_.database_name.has_value()) run_p.db = session_params_.database_name;
        if (session_params_.impersonated_user.has_value()) run_p.imp_user = session_params_.impersonated_user;
        // For Bolt < 5.0, access mode is part of RUN. For >= 5.0, it's part of BEGIN.
        // Here, for auto-commit, we can set it in RUN if applicable.
        if (conn->get_bolt_version() < boltprotocol::versions::Version(5, 0)) {
            if (session_params_.default_access_mode == config::AccessMode::READ) run_p.mode = "r";
        }
        if (tx_metadata.has_value()) run_p.tx_metadata = *tx_metadata;

        std::vector<uint8_t> run_payload;
        boltprotocol::PackStreamWriter run_writer(run_payload);
        boltprotocol::BoltError err = boltprotocol::serialize_run_message(run_p, run_writer, conn->get_bolt_version());
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Auto-commit RUN serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending auto-commit RUN.", conn->get_id());
        err = conn->send_request_receive_summary(run_payload, out_run_summary, out_failure);

        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Auto-commit RUN send/receive summary", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }
        if (conn->get_last_error() != boltprotocol::BoltError::SUCCESS) {
            std::string server_fail_detail = error::format_server_failure(out_failure);
            std::string msg = error::format_error_message("Auto-commit RUN server failure", conn->get_last_error(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error(), msg);
            return {conn->get_last_error(), msg};
        }
        if (logger) logger->trace("[SessionStream {}] Auto-commit RUN successful, got summary.", conn->get_id());
        return {boltprotocol::BoltError::SUCCESS, ""};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_prepare_explicit_tx_stream(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, boltprotocol::SuccessMessageParams& out_run_success, boltprotocol::FailureMessageParams& out_failure) {
        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_prepare_explicit_tx_stream");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        boltprotocol::RunMessageParams run_p;
        run_p.cypher_query = cypher;
        run_p.parameters = parameters;

        std::vector<uint8_t> run_payload;
        boltprotocol::PackStreamWriter run_w(run_payload);
        boltprotocol::BoltError err = boltprotocol::serialize_run_message(run_p, run_w, conn->get_bolt_version());
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Explicit TX RUN serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending explicit TX RUN.", conn->get_id());
        err = conn->send_request_receive_summary(run_payload, out_run_success, out_failure);

        if (err == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error() == boltprotocol::BoltError::SUCCESS) {
                auto it_qid = out_run_success.metadata.find("qid");
                // Use Version constructor for comparison
                if (it_qid != out_run_success.metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
                    current_transaction_query_id_ = std::get<int64_t>(it_qid->second);
                    if (logger) logger->trace("[SessionStream {}] Explicit TX RUN successful, qid: {}.", conn->get_id(), *current_transaction_query_id_);
                } else if (!(conn->get_bolt_version() < boltprotocol::versions::Version(3, 0))) {
                    std::string msg = "Missing qid in RUN SUCCESS for explicit transaction (Bolt >= 3.0).";
                    _invalidate_session_due_to_connection_error(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg);
                    return {boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg};
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string server_fail_detail = error::format_server_failure(out_failure);
                std::string msg = error::format_error_message("Explicit TX RUN server failure", conn->get_last_error(), server_fail_detail);
                _invalidate_session_due_to_connection_error(conn->get_last_error(), msg);
                return {conn->get_last_error(), msg};
            }
        }
        std::string msg = error::format_error_message("Explicit TX RUN send/receive summary", err, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err, msg);
        return {err, msg};
    }

    // ... (_stream_pull_records, _stream_discard_records as previously corrected) ...
    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_stream_pull_records(std::optional<int64_t> qid, int64_t n, std::vector<boltprotocol::RecordMessageParams>& out_records, boltprotocol::SuccessMessageParams& out_pull_summary) {
        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_stream_pull_records");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        boltprotocol::PullMessageParams pull_p;
        pull_p.n = n;
        pull_p.qid = qid;

        std::vector<uint8_t> pull_payload;
        boltprotocol::PackStreamWriter writer(pull_payload);
        boltprotocol::BoltError err = boltprotocol::serialize_pull_message(pull_p, writer);
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("PULL serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending PULL (n={}, qid={}).", conn->get_id(), n, qid.has_value() ? std::to_string(qid.value()) : "none");

        boltprotocol::FailureMessageParams failure_details;
        auto record_processor = [&out_records, logger, conn_id = conn->get_id()](boltprotocol::MessageTag, const std::vector<uint8_t>& rec_payload, internal::BoltPhysicalConnection&) {
            boltprotocol::RecordMessageParams rec;
            boltprotocol::PackStreamReader r(rec_payload);
            if (boltprotocol::deserialize_record_message(r, rec) == boltprotocol::BoltError::SUCCESS) {
                out_records.push_back(std::move(rec));
                return boltprotocol::BoltError::SUCCESS;
            }
            if (logger) logger->error("[SessionStream {}] Failed to deserialize RECORD message.", conn_id);
            return boltprotocol::BoltError::DESERIALIZATION_ERROR;
        };

        err = conn->send_request_receive_stream(pull_payload, record_processor, out_pull_summary, failure_details);

        if (err == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error() == boltprotocol::BoltError::SUCCESS) {
                if (!is_in_transaction()) {
                    auto it_bookmark = out_pull_summary.metadata.find("bookmark");
                    if (it_bookmark != out_pull_summary.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                        update_bookmarks({std::get<std::string>(it_bookmark->second)});
                    } else {
                        update_bookmarks({});
                    }
                }
                if (logger) {
                    bool has_more = false;
                    auto it_has_more = out_pull_summary.metadata.find("has_more");
                    if (it_has_more != out_pull_summary.metadata.end() && std::holds_alternative<bool>(it_has_more->second)) {
                        has_more = std::get<bool>(it_has_more->second);
                    }
                    logger->trace("[SessionStream {}] PULL successful. Records received: {}. HasMore: {}", conn->get_id(), out_records.size(), has_more);
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string server_fail_detail = error::format_server_failure(failure_details);
                std::string msg = error::format_error_message("PULL server failure", conn->get_last_error(), server_fail_detail);
                _invalidate_session_due_to_connection_error(conn->get_last_error(), msg);
                return {conn->get_last_error(), msg};
            }
        }
        std::string msg = error::format_error_message("PULL stream processing", err, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err, msg);
        return {err, msg};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_stream_discard_records(std::optional<int64_t> qid, int64_t n, boltprotocol::SuccessMessageParams& out_discard_summary) {
        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_stream_discard_records");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        boltprotocol::DiscardMessageParams discard_p;
        discard_p.n = n;
        discard_p.qid = qid;

        std::vector<uint8_t> discard_payload;
        boltprotocol::PackStreamWriter writer(discard_payload);
        boltprotocol::BoltError err = boltprotocol::serialize_discard_message(discard_p, writer);
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("DISCARD serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending DISCARD (n={}, qid={}).", conn->get_id(), n, qid.has_value() ? std::to_string(qid.value()) : "none");

        boltprotocol::FailureMessageParams failure_details;
        err = conn->send_request_receive_summary(discard_payload, out_discard_summary, failure_details);

        if (err == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error() == boltprotocol::BoltError::SUCCESS) {
                if (logger) logger->trace("[SessionStream {}] DISCARD successful.", conn->get_id());
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string server_fail_detail = error::format_server_failure(failure_details);
                std::string msg = error::format_error_message("DISCARD server failure", conn->get_last_error(), server_fail_detail);
                _invalidate_session_due_to_connection_error(conn->get_last_error(), msg);
                return {conn->get_last_error(), msg};
            }
        }
        std::string msg = error::format_error_message("DISCARD send/receive summary", err, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err, msg);
        return {err, msg};
    }

}  // namespace neo4j_bolt_transport