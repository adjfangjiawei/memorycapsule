#include <chrono>    // For std::chrono::milliseconds
#include <iostream>  // 调试用
#include <utility>   // For std::move

#include "boltprotocol/message_serialization.h"  // For serialize_..._message
#include "boltprotocol/packstream_writer.h"      // For PackStreamWriter
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"  // <--- 添加这一行
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    // --- Explicit Transaction Methods ---
    std::pair<boltprotocol::BoltError, std::string> SessionHandle::begin_transaction(const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout_opt) {
        std::pair<boltprotocol::BoltError, std::string> conn_check_result;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_result, "begin_transaction");
        if (!conn) {
            return conn_check_result;
        }
        auto logger = conn->get_logger();  // conn is valid here

        if (in_explicit_transaction_) {
            if (logger) logger->warn("[SessionTX {}] Attempt to begin transaction while already in one.", conn->get_id());
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Cannot begin transaction; already in an explicit transaction."};
        }

        boltprotocol::BeginMessageParams params;
        params.bookmarks = current_bookmarks_;
        if (session_params_.database_name.has_value()) {
            params.db = session_params_.database_name;
        }
        if (session_params_.impersonated_user.has_value()) {
            params.imp_user = session_params_.impersonated_user;
        }

        // Access mode (Bolt 5.0+)
        if (!(conn->get_bolt_version() < boltprotocol::versions::V5_0)) {
            if (session_params_.default_access_mode == config::AccessMode::READ) {
                params.other_extra_fields["mode"] = std::string("r");
            }
        }

        if (tx_metadata.has_value()) {
            params.tx_metadata = *tx_metadata;
        }
        if (tx_timeout_opt.has_value()) {
            params.tx_timeout = static_cast<int64_t>(tx_timeout_opt.value().count());
        }

        std::vector<uint8_t> begin_payload_bytes;
        boltprotocol::PackStreamWriter writer(begin_payload_bytes);
        boltprotocol::BoltError err_code = boltprotocol::serialize_begin_message(params, writer, conn->get_bolt_version());
        if (err_code != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("BEGIN serialization", err_code);
            _invalidate_session_due_to_connection_error(err_code, msg);
            return {err_code, msg};
        }

        boltprotocol::SuccessMessageParams success_meta_raw;
        boltprotocol::FailureMessageParams failure_meta_raw;
        err_code = conn->send_request_receive_summary(begin_payload_bytes, success_meta_raw, failure_meta_raw);

        if (err_code == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
                in_explicit_transaction_ = true;
                current_transaction_query_id_.reset();
                if (logger) {
                    logger->info("[SessionTX {}] Transaction started. DB: '{}', Mode: '{}', Timeout: {}ms, Meta: {}",
                                 conn->get_id(),
                                 params.db.value_or("<default>"),
                                 (session_params_.default_access_mode == config::AccessMode::READ ? "READ" : "WRITE"),
                                 params.tx_timeout.has_value() ? std::to_string(params.tx_timeout.value()) : "N/A",
                                 params.tx_metadata.has_value() && !params.tx_metadata.value().empty() ? "Yes" : "No");
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string server_fail_msg = error::format_server_failure(failure_meta_raw);
                std::string msg = error::format_error_message("BEGIN failed on server", conn->get_last_error_code(), server_fail_msg);
                _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
                return {conn->get_last_error_code(), msg};
            }
        }
        std::string msg = error::format_error_message("BEGIN send/receive", err_code, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err_code, msg);
        return {err_code, msg};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::commit_transaction() {
        std::pair<boltprotocol::BoltError, std::string> conn_check_result;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_result, "commit_transaction");
        if (!conn) return conn_check_result;
        auto logger = conn->get_logger();  // conn is valid here

        if (!in_explicit_transaction_) {
            if (logger) logger->warn("[SessionTX {}] Attempt to commit transaction while not in one.", conn->get_id());
            return {boltprotocol::BoltError::INVALID_ARGUMENT, "Cannot commit: not in an explicit transaction."};
        }

        std::vector<uint8_t> commit_payload_bytes;
        boltprotocol::PackStreamWriter writer(commit_payload_bytes);
        boltprotocol::BoltError err_code = boltprotocol::serialize_commit_message(writer);
        if (err_code != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("COMMIT serialization", err_code);
            _invalidate_session_due_to_connection_error(err_code, msg);
            in_explicit_transaction_ = false;
            current_transaction_query_id_.reset();
            return {err_code, msg};
        }

        boltprotocol::SuccessMessageParams success_meta_raw;
        boltprotocol::FailureMessageParams failure_meta_raw;
        err_code = conn->send_request_receive_summary(commit_payload_bytes, success_meta_raw, failure_meta_raw);

        in_explicit_transaction_ = false;
        current_transaction_query_id_.reset();

        if (err_code == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
                auto it_bookmark = success_meta_raw.metadata.find("bookmark");
                if (it_bookmark != success_meta_raw.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
                    update_bookmarks({std::get<std::string>(it_bookmark->second)});
                } else {
                    if (logger) logger->trace("[SessionTX {}] COMMIT successful but no bookmark returned (Bolt version: {}.{}).", conn->get_id(), (int)conn->get_bolt_version().major, (int)conn->get_bolt_version().minor);
                    update_bookmarks({});
                }
                if (logger) {
                    logger->info("[SessionTX {}] Transaction committed. New bookmark: {}", conn->get_id(), current_bookmarks_.empty() ? "<none>" : current_bookmarks_[0]);
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string msg = error::format_error_message("COMMIT failed on server", conn->get_last_error_code(), error::format_server_failure(failure_meta_raw));
                _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
                return {conn->get_last_error_code(), msg};
            }
        }
        std::string msg = error::format_error_message("COMMIT send/receive", err_code, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err_code, msg);
        return {err_code, msg};
    }

    std::pair<boltprotocol::BoltError, std::string> SessionHandle::rollback_transaction() {
        std::pair<boltprotocol::BoltError, std::string> conn_check_result;
        internal::BoltPhysicalConnection* conn = _get_valid_connection_for_operation(conn_check_result, "rollback_transaction (pre-check)");

        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (conn)
            logger = conn->get_logger();
        else if (transport_manager_ && transport_manager_->get_config().logger)  // transport_manager_ is checked
            logger = transport_manager_->get_config().logger;

        if (!in_explicit_transaction_) {
            if (logger) logger->trace("[SessionTX {}] Rollback called when not in an explicit transaction. No-op.", (conn ? conn->get_id() : 0));
            return {boltprotocol::BoltError::SUCCESS, ""};
        }

        if (!conn) {
            std::string msg = "Rollback attempt with no valid connection while in TX: " + conn_check_result.second;
            if (logger) logger->warn("[SessionTX Rollback] {}", msg);
            _invalidate_session_due_to_connection_error(conn_check_result.first, msg);
            in_explicit_transaction_ = false;
            current_transaction_query_id_.reset();
            return {conn_check_result.first, msg};
        }

        std::vector<uint8_t> rollback_payload_bytes;
        boltprotocol::PackStreamWriter writer(rollback_payload_bytes);
        boltprotocol::BoltError err_code = boltprotocol::serialize_rollback_message(writer);
        if (err_code != boltprotocol::BoltError::SUCCESS) {
            std::string msg = error::format_error_message("ROLLBACK serialization", err_code);
            _invalidate_session_due_to_connection_error(err_code, msg);
            in_explicit_transaction_ = false;
            current_transaction_query_id_.reset();
            return {err_code, msg};
        }

        boltprotocol::SuccessMessageParams success_meta_raw;
        boltprotocol::FailureMessageParams failure_meta_raw;
        err_code = conn->send_request_receive_summary(rollback_payload_bytes, success_meta_raw, failure_meta_raw);

        in_explicit_transaction_ = false;
        current_transaction_query_id_.reset();

        if (err_code == boltprotocol::BoltError::SUCCESS) {
            if (conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
                if (logger) {
                    logger->info("[SessionTX {}] Transaction rolled back.", conn->get_id());
                }
                return {boltprotocol::BoltError::SUCCESS, ""};
            } else {
                std::string msg = error::format_error_message("ROLLBACK failed on server", conn->get_last_error_code(), error::format_server_failure(failure_meta_raw));
                _invalidate_session_due_to_connection_error(conn->get_last_error_code(), msg);
                return {conn->get_last_error_code(), msg};
            }
        }
        std::string msg = error::format_error_message("ROLLBACK send/receive", err_code, conn->get_last_error_message());
        _invalidate_session_due_to_connection_error(err_code, msg);
        return {err_code, msg};
    }

}  // namespace neo4j_bolt_transport