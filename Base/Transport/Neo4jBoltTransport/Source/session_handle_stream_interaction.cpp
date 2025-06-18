#include <iostream>  // 调试用
#include <utility>   // For std::move

#include "boltprotocol/message_serialization.h"  // For serialize_..._message
#include "boltprotocol/packstream_writer.h"      // For PackStreamWriter
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::_prepare_auto_commit_run(const std::string& cypher,
                                                                                            const std::map<std::string, boltprotocol::Value>& parameters,
                                                                                            const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata,  // For auto-commit, this is tx_metadata for the implicit transaction
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

        // Access mode for auto-commit (Bolt < 5.0)
        if (conn->get_bolt_version() < boltprotocol::versions::Version(5, 0)) {
            if (session_params_.default_access_mode == config::AccessMode::READ) {
                run_p.mode = "r";
            }
            // WRITE is default, so no explicit "w"
        }
        // For Bolt 5.0+, access mode for auto-commit is implicit or server default.
        // If specific mode is needed, explicit transaction is preferred.

        // Transaction metadata and timeout for the implicit transaction of RUN
        if (tx_metadata.has_value()) {
            run_p.tx_metadata = *tx_metadata;
        }
        // Apply session-configured default timeout if no override and applicable
        // Note: The RunMessageParams struct in bolt_message_params.h currently has tx_timeout.
        // The Bolt spec details that tx_timeout in RUN is for the implicit transaction.
        // This seems correct. tx_timeout is not part of SessionParameters directly,
        // but can be passed via tx_metadata_override in run_query.
        // If a global default tx_timeout for auto-commit is desired, it should be part of SessionParameters or TransportConfig.
        // For now, we assume tx_timeout is only passed via tx_metadata_override if present.
        // If tx_metadata_override has "tx_timeout", use it.
        // Let's clarify if tx_timeout should be a top-level optional in SessionParameters or RunMessageParams.
        // Given current structure, if tx_metadata has a 'tx_timeout' key, it would be used.
        // Let's assume RunMessageParams::tx_timeout is directly settable.

        // If a timeout is specified via a `tx_timeout` key within `tx_metadata` (less common for RUN, more for BEGIN),
        // or if `RunMessageParams` directly had a `tx_timeout_override` field.
        // The current `RunMessageParams` has `std::optional<int64_t> tx_timeout;`
        // So, if `tx_metadata` (the argument to this function) is used to pass a timeout, it should be extracted.
        // Or, `run_query` should pass a separate timeout parameter.
        // For now, assume tx_timeout if present in tx_metadata is not standard for RUN.
        // If a specific tx_timeout is needed for auto-commit, it should be part of RunMessageParams directly.
        // Let's assume for now `RunMessageParams::tx_timeout` is set if `tx_metadata_override` contained a timeout in `run_query`.
        // This part needs careful alignment with how `run_query` calls this.
        // _Current RunMessageParams has `tx_timeout`. So, if run_query's tx_metadata_override is used
        // to populate run_p.tx_timeout, it's fine. _
        // _If not, session_params might have a default auto-commit timeout._
        // This seems like a detail to refine based on desired API for auto-commit timeouts.
        // For now, rely on RunMessageParams.tx_timeout being populated correctly by the caller if needed.

        std::vector<uint8_t> run_payload_bytes;
        boltprotocol::PackStreamWriter run_writer(run_payload_bytes);
        boltprotocol::BoltError err = boltprotocol::serialize_run_message(run_p, run_writer, conn->get_bolt_version());
        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Auto-commit RUN serialization", err);
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (logger) logger->trace("[SessionStream {}] Sending auto-commit RUN (no immediate PULL/DISCARD). Cypher: {:.30}", conn->get_id(), cypher);
        err = conn->send_request_receive_summary(run_payload_bytes, out_run_summary_raw, out_failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("Auto-commit RUN send/receive summary", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }
        if (conn->get_last_error_code() != boltprotocol::BoltError::SUCCESS) {
            std::string server_fail_detail = error::format_server_failure(out_failure_details_raw);
            std::string msg = error::format_error_message("Auto-commit RUN server failure", conn->get_last_error_code(), server_fail_detail);
            // Server failure for RUN doesn't always invalidate the *connection*, but the *operation* failed.
            // The session might still be usable for other queries if the error was statement-specific.
            // However, _invalidate_session_due_to_connection_error also sets session's connection_is_valid_ to false.
            // This behavior might be too aggressive for statement errors.
            // For now, keep it, but consider refining _invalidate_session... for different error types.
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }

        // Bookmarks are updated after PULL/DISCARD for auto-commit, not directly after RUN.
        // The RUN summary might contain `fields` and `qid`.
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
        // In an explicit transaction, RUN does not include bookmarks, db, imp_user, mode, tx_metadata, or tx_timeout.
        // These are set during BEGIN.

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
            if (!(conn->get_bolt_version() < boltprotocol::versions::Version(4, 0))) {  // Bolt 4.0+
                auto it_qid = out_run_summary_raw.metadata.find("qid");
                if (it_qid != out_run_summary_raw.metadata.end() && std::holds_alternative<int64_t>(it_qid->second)) {
                    current_transaction_query_id_ = std::get<int64_t>(it_qid->second);
                    if (logger) logger->trace("[SessionStream {}] Explicit TX RUN successful, qid: {}.", conn->get_id(), *current_transaction_query_id_);
                } else {
                    std::string msg = "Missing qid in RUN SUCCESS for explicit transaction (Bolt >= 4.0).";
                    _invalidate_session_due_to_connection_error(boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg);
                    return {boltprotocol::BoltError::INVALID_MESSAGE_FORMAT, msg};
                }
            } else {  // Bolt < 4.0
                if (logger) logger->trace("[SessionStream {}] Explicit TX RUN successful (Bolt < 4.0, no qid expected from RUN).", conn->get_id());
            }
            return {boltprotocol::BoltError::SUCCESS, ""};
        } else {  // Server returned FAILURE for RUN
            std::string server_fail_detail = error::format_server_failure(out_failure_details_raw);
            std::string msg = error::format_error_message("Explicit TX RUN server failure", conn->get_last_error_code(), server_fail_detail);
            // Similar to auto-commit, server failure for RUN might not invalidate the whole connection/transaction
            // if it's a statement error. But _invalidate_session_due_to_connection_error is currently aggressive.
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

        if (logger) logger->trace("[SessionStream {}] Sending PULL (n={}, qid={}).", conn->get_id(), n, qid.has_value() ? std::to_string(qid.value()) : "none");

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
            // PULL successful, records are in out_records, summary in out_pull_summary_raw
            // Update bookmarks only if NOT in an explicit transaction
            if (!is_in_transaction()) {
                auto it_bookmark = out_pull_summary_raw.metadata.find("bookmark");
                if (it_bookmark != out_pull_summary_raw.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                    update_bookmarks({std::get<std::string>(it_bookmark->second)});
                    if (logger) logger->trace("[SessionStream {}] Bookmarks updated after PULL: {}", conn->get_id(), std::get<std::string>(it_bookmark->second));
                } else {
                    update_bookmarks({});  // Clear bookmarks if not returned
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
        } else {  // Server returned FAILURE for PULL
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

        if (logger) logger->trace("[SessionStream {}] Sending DISCARD (n={}, qid={}).", conn->get_id(), n, qid.has_value() ? std::to_string(qid.value()) : "none");

        boltprotocol::FailureMessageParams failure_details_raw;
        err = conn->send_request_receive_summary(discard_payload_bytes, out_discard_summary_raw, failure_details_raw);

        if (err != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("DISCARD send/receive summary", err, conn->get_last_error_message());
            _invalidate_session_due_to_connection_error(err, msg);
            return {err, msg};
        }

        if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
            // DISCARD successful, summary in out_discard_summary_raw
            // Update bookmarks only if NOT in an explicit transaction
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
        } else {  // Server returned FAILURE for DISCARD
            std::string server_fail_detail = error::format_server_failure(failure_details_raw);
            std::string msg = error::format_error_message("DISCARD server failure", conn->get_last_error_code(), server_fail_detail);
            _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
            return {conn->get_last_error_code(), msg};
        }
    }

}  // namespace neo4j_bolt_transport