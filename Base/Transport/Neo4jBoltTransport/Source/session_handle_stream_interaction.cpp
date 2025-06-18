#include <iostream>
#include <utility>

#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_writer.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_prepare_auto_commit_run(const std::string& cypher,
                                                                                            const std::map<std::string, boltprotocol::Value>& parameters,
                                                                                            const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata,
                                                                                            const std::optional<std::chrono::milliseconds>& tx_timeout,
                                                                                            boltprotocol::SuccessMessageParams& out_run_summary_raw,
                                                                                            boltprotocol::FailureMessageParams& out_failure_details_raw) {
        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_prepare_auto_commit_run");
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

        // Compare with a constructed Version object for Bolt 5.0
        if (conn->get_bolt_version() < boltprotocol::versions::Version(5, 0)) {
            if (session_params_.default_access_mode == config::AccessMode::READ) {
                run_p.mode = "r";
            }
        }

        if (tx_metadata.has_value()) {
            run_p.tx_metadata = tx_metadata.value();
        }
        if (tx_timeout.has_value()) {
            run_p.tx_timeout = static_cast<int64_t>(tx_timeout.value().count());
        }

        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_writer(run_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_run_message(run_p, run_writer, conn->get_bolt_version());
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Auto-commit RUN serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger)
            logger->trace(
                "[SessionStream {}] Sending auto-commit RUN. Cypher: {:.30}, Timeout: {}ms, Meta: {}", conn->get_id(), cypher, run_p.tx_timeout.has_value() ? std::to_string(run_p.tx_timeout.value()) : "N/A", run_p.tx_metadata.has_value() && !run_p.tx_metadata.value().empty() ? "Yes" : "No");

        err = conn->send_request_receive_summary(run_payload_bytes, out_run_summary_raw, out_failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Auto-commit RUN send/receive summary", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }
        if (conn->get_last_error_code() != boltprotocol::BoltError::SUCCESS) {
            std::string server_fail_detail = error::format_server_failure(out_failure_details_raw);
            std::string msg = error::format_error_message("Auto-commit RUN server failure", conn->get_last_error_code(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }

        if (logger) logger->trace("[SessionStream {}] Auto-commit RUN successful, got its summary.", conn->get_id());
        return {boltprotocol::BoltError::SUCCESS, ""};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_prepare_explicit_tx_run(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, boltprotocol::SuccessMessageParams& out_run_summary_raw, boltprotocol::FailureMessageParams& out_failure_details_raw) {
        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_prepare_explicit_tx_run");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        if (!in_explicit_transaction_) {
            if (logger) logger->warn("[SessionStream {}] _prepare_explicit_tx_run called when not in transaction.", conn->get_id());
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Cannot run query in explicit TX mode; not in transaction."};
        }

        boltprotocol::RunMessageParams run_p;
        run_p.cypher_query = cypher;
        run_p.parameters = parameters;

        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_w(run_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_run_message(run_p, run_w, conn->get_bolt_version());
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Explicit TX RUN serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending explicit TX RUN. Cypher: {:.30}", conn->get_id(), cypher);
        err = conn->send_request_receive_summary(run_payload_bytes, out_run_summary_raw, out_failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Explicit TX RUN send/receive summary", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
            current_transaction_query_id_.reset();
            // Compare with a constructed Version object for Bolt 4.0
            if (!(conn->get_bolt_version() < boltprotocol::versions::Version(4, 0))) {  // If Bolt version is >= 4.0
                auto it_qid = out_run_summary_raw.metadata.find("qid");
                if (it_qid != out_run_summary_raw.metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
                    current_transaction_query_id_ = std::get<int64_t>(it_qid->second);
                    if (logger) logger->trace("[SessionStream {}] Explicit TX RUN successful, qid: {}.", conn->get_id(), *current_transaction_query_id_);
                } else {
                    if (logger) logger->warn("[SessionStream {}] Missing qid in RUN SUCCESS for explicit transaction (Bolt version {}.{}). Subsequent PULL/DISCARD may need to be implicit.", conn->get_id(), (int)conn->get_bolt_version().major, (int)conn->get_bolt_version().minor);
                }
            } else {
                if (logger) logger->trace("[SessionStream {}] Explicit TX RUN successful (Bolt < 4.0, no qid expected from RUN).", conn->get_id());
            }
            return {boltprotocol::BoltError::SUCCESS, ""};
        } else {
            std::string server_fail_detail = error::format_server_failure(out_failure_details_raw);
            std::string msg = error::format_error_message("Explicit TX RUN server failure", conn->get_last_error_code(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_stream_pull_records(std::optional<int64_t> qid, int64_t n, std::vector<boltprotocol::RecordMessageParams>& out_records, boltprotocol::SuccessMessageParams& out_pull_summary_raw) {
        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_stream_pull_records");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        boltprotocol::PullMessageParams pull_p;
        pull_p.n = n;
        pull_p.qid = qid;

        std::vector<uint8_t> pull_payload_bytes;
        boltprotocol::PackStreamWriter writer(pull_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_pull_message(pull_p, writer);
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("PULL serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending PULL (n={}, qid={}).", conn->get_id(), n, qid.has_value() ? std::to_string(qid.value()) : "implicit");

        boltprotocol::FailureMessageParams failure_details_raw;

        auto record_processor = [&](boltprotocol::MessageTag /*tag*/, const std::vector<uint8_t>& rec_payload, internal::BoltPhysicalConnection& /*connection_ref*/) {
            boltprotocol::RecordMessageParams rec;
            boltprotocol::PackStreamReader r(rec_payload);
            if (boltprotocol::deserialize_record_message(r, rec) == boltprotocol::BoltError::SUCCESS) {
                out_records.push_back(std::move(rec));
                return boltprotocol::BoltError::SUCCESS;
            }
            if (logger) logger->error("[SessionStream {}] Failed to deserialize RECORD message during PULL.", conn->get_id());
            return boltprotocol::BoltError::DESERIALIZATION_ERROR;
        };

        err = conn->send_request_receive_stream(pull_payload_bytes, record_processor, out_pull_summary_raw, failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("PULL stream processing", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
            if (!is_in_transaction()) {
                auto it_bookmark = out_pull_summary_raw.metadata.find("bookmark");
                if (it_bookmark != out_pull_summary_raw.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                    update_bookmarks({std::get<std::string>(it_bookmark->second)});
                    if (logger) logger->trace("[SessionStream {}] Bookmarks updated after PULL: {}", conn->get_id(), std::get<std::string>(it_bookmark->second));
                } else {
                    update_bookmarks({});
                    if (logger) logger->trace("[SessionStream {}] No bookmark returned after PULL, bookmarks cleared.", conn->get_id());
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
        } else {
            std::string server_fail_detail = error::format_server_failure(failure_details_raw);
            std::string msg = error::format_error_message("PULL server failure", conn->get_last_error_code(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_stream_discard_records(std::optional<int64_t> qid, int64_t n, boltprotocol::SuccessMessageParams& out_discard_summary_raw) {
        std::pair<boltprotocol::BoltError, std::string> conn_err_pair;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_err_pair, "_stream_discard_records");
        if (!conn) {
            return conn_err_pair;
        }
        auto logger = conn->get_logger();

        boltprotocol::DiscardMessageParams discard_p;
        discard_p.n = n;
        discard_p.qid = qid;

        std::vector<uint8_t> discard_payload_bytes;
        boltprotocol::PackStreamWriter writer(discard_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_discard_message(discard_p, writer);
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("DISCARD serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending DISCARD (n={}, qid={}).", conn->get_id(), n, qid.has_value() ? std::to_string(qid.value()) : "implicit");

        boltprotocol::FailureMessageParams failure_details_raw;
        err = conn->send_request_receive_summary(discard_payload_bytes, out_discard_summary_raw, failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("DISCARD send/receive summary", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
            if (!is_in_transaction()) {
                auto it_bookmark = out_discard_summary_raw.metadata.find("bookmark");
                if (it_bookmark != out_discard_summary_raw.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                    update_bookmarks({std::get<std::string>(it_bookmark->second)});
                    if (logger) logger->trace("[SessionStream {}] Bookmarks updated after DISCARD: {}", conn->get_id(), std::get<std::string>(it_bookmark->second));
                } else {
                    update_bookmarks({});
                    if (logger) logger->trace("[SessionStream {}] No bookmark returned after DISCARD, bookmarks cleared.", conn->get_id());
                }
            }
            if (logger) logger->trace("[SessionStream {}] DISCARD successful.", conn->get_id());
            return {boltprotocol::BoltError::SUCCESS, ""};
        } else {
            std::string server_fail_detail = error::format_server_failure(failure_details_raw);
            std::string msg = error::format_error_message("DISCARD server failure", conn->get_last_error_code(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }
    }

}  // namespace neo4j_bolt_transport