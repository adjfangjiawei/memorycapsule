#include <iostream>  // For debug, replace with logging
#include <utility>   // For std::move

#include "neo4j_bolt_transport/config/session_parameters.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // For error formatting
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"    // For Neo4jBoltTransport
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    SessionHandle::SessionHandle(Neo4jBoltTransport* transport_mgr, internal::BoltPhysicalConnection::PooledConnection conn_ptr, config::SessionParameters params_val)
        : transport_manager_(transport_mgr), connection_(std::move(conn_ptr)), session_params_(std::move(params_val)), current_bookmarks_(session_params_.initial_bookmarks) {
        std::shared_ptr<spdlog::logger> drv_logger = transport_manager_ ? transport_manager_->get_config().logger : nullptr;

        if (!transport_manager_) {
            connection_is_valid_ = false;
            is_closed_ = true;
            if (drv_logger) drv_logger->error("[SessionLC] SessionHandle created without a valid transport manager.");
            // connection_ will be nullptr, _release_connection_to_pool will do nothing.
            return;
        }

        std::shared_ptr<spdlog::logger> conn_logger = connection_ ? connection_->get_logger() : nullptr;
        if (!conn_logger && drv_logger) conn_logger = drv_logger;  // Fallback

        if (!connection_ || !connection_->is_ready_for_queries()) {
            boltprotocol::BoltError last_err = connection_ ? connection_->get_last_error() : boltprotocol::BoltError::NETWORK_ERROR;
            std::string last_err_msg = connection_ ? connection_->get_last_error_message() : "Connection pointer null or not ready at SessionHandle construction.";
            if (conn_logger) conn_logger->warn("[SessionLC {}] Connection not ready at SessionHandle construction. Error: {}, Msg: {}", connection_ ? connection_->get_id() : 0, static_cast<int>(last_err), last_err_msg);
            _invalidate_session_due_to_connection_error(last_err, "SessionHandle construction: " + last_err_msg);
            _release_connection_to_pool(false);  // Release potentially bad connection
        } else {
            connection_->mark_as_used();
            if (conn_logger) conn_logger->debug("[SessionLC {}] SessionHandle constructed with ready connection.", connection_->get_id());
        }
    }

    SessionHandle::~SessionHandle() {
        std::shared_ptr<spdlog::logger> logger = (connection_ && connection_->get_logger()) ? connection_->get_logger() : (transport_manager_ ? transport_manager_->get_config().logger : nullptr);
        if (logger) logger->debug("[SessionLC {}] SessionHandle destructing. Closed: {}, InTx: {}", (connection_ ? connection_->get_id() : 0), is_closed_, in_explicit_transaction_);
        close();
    }

    SessionHandle::SessionHandle(SessionHandle&& other) noexcept
        : transport_manager_(other.transport_manager_),
          connection_(std::move(other.connection_)),
          session_params_(std::move(other.session_params_)),
          in_explicit_transaction_(other.in_explicit_transaction_),
          current_transaction_query_id_(other.current_transaction_query_id_),
          current_bookmarks_(std::move(other.current_bookmarks_)),
          is_closed_(other.is_closed_),
          connection_is_valid_(other.connection_is_valid_) {
        std::shared_ptr<spdlog::logger> logger = (connection_ && connection_->get_logger()) ? connection_->get_logger() : nullptr;
        if (!logger && transport_manager_) logger = transport_manager_->get_config().logger;
        if (logger) logger->trace("[SessionLC {}] SessionHandle move constructed from old SessionHandle.", (connection_ ? connection_->get_id() : 0));

        other.transport_manager_ = nullptr;  // Other is now invalid
        // other.connection_ is already moved
        other.is_closed_ = true;
        other.connection_is_valid_ = false;
    }

    SessionHandle& SessionHandle::operator=(SessionHandle&& other) noexcept {
        if (this != &other) {
            std::shared_ptr<spdlog::logger> logger = (connection_ && connection_->get_logger()) ? connection_->get_logger() : nullptr;
            if (!logger && transport_manager_) logger = transport_manager_->get_config().logger;
            if (logger) logger->trace("[SessionLC {}] SessionHandle move assigning from other SessionHandle.", (connection_ ? connection_->get_id() : 0));

            close();  // Close current session first

            transport_manager_ = other.transport_manager_;
            connection_ = std::move(other.connection_);
            session_params_ = std::move(other.session_params_);
            in_explicit_transaction_ = other.in_explicit_transaction_;
            current_transaction_query_id_ = other.current_transaction_query_id_;
            current_bookmarks_ = std::move(other.current_bookmarks_);
            is_closed_ = other.is_closed_;
            connection_is_valid_ = other.connection_is_valid_;

            other.transport_manager_ = nullptr;  // Other is now invalid
            other.is_closed_ = true;
            other.connection_is_valid_ = false;
        }
        return *this;
    }

    void SessionHandle::_release_connection_to_pool(bool mark_healthy) {
        if (connection_ && transport_manager_) {
            std::shared_ptr<spdlog::logger> logger = connection_->get_logger();
            uint64_t conn_id = connection_->get_id();
            if (logger) logger->trace("[SessionLC {}] Releasing connection {} to pool. Healthy: {}", conn_id, conn_id, mark_healthy && connection_is_valid_);
            transport_manager_->release_connection(std::move(connection_), mark_healthy && connection_is_valid_);
            // connection_ is now null
        }
        connection_is_valid_ = false;  // After release, session no longer has a valid connection
    }

    void SessionHandle::close() {
        if (is_closed_) {
            return;
        }

        std::shared_ptr<spdlog::logger> logger = (connection_ && connection_->get_logger()) ? connection_->get_logger() : (transport_manager_ ? transport_manager_->get_config().logger : nullptr);
        if (logger) logger->debug("[SessionLC {}] Closing SessionHandle. InTx: {}", (connection_ ? connection_->get_id() : 0), in_explicit_transaction_);

        if (in_explicit_transaction_ && connection_is_valid_ && connection_ && connection_->is_ready_for_queries()) {
            if (logger) logger->info("[SessionLC {}] Rolling back active transaction during close.", connection_->get_id());
            rollback_transaction();  // This will also set in_explicit_transaction_ = false
        }
        _release_connection_to_pool(connection_is_valid_);  // Release with current validity status
        is_closed_ = true;
    }

    void SessionHandle::_invalidate_session_due_to_connection_error(boltprotocol::BoltError error, const std::string& context_message) {
        connection_is_valid_ = false;
        std::shared_ptr<spdlog::logger> logger = (connection_ && connection_->get_logger()) ? connection_->get_logger() : (transport_manager_ ? transport_manager_->get_config().logger : nullptr);
        if (logger) {
            logger->warn("[SessionLC {}] Session invalidated due to connection error. Code: {}, Context: {}", (connection_ ? connection_->get_id() : 0), static_cast<int>(error), context_message);
        }
        // BoltPhysicalConnection itself would have logged its detailed error and possibly marked itself defunct.
    }

    internal::BoltPhysicalConnection* SessionHandle::_get_valid_connection_for_operation(std::pair<boltprotocol::BoltError, std::string>& out_err_pair, const std::string& operation_context) {
        std::shared_ptr<spdlog::logger> drv_logger = transport_manager_ ? transport_manager_->get_config().logger : nullptr;

        if (is_closed_) {
            out_err_pair = {boltprotocol::BoltError::INVALID_ARGUMENT, "Operation on closed session: " + operation_context};
            if (drv_logger) drv_logger->warn("[SessionOp] {}", out_err_pair.second);
            return nullptr;
        }
        if (!connection_is_valid_ || !connection_) {
            out_err_pair = {boltprotocol::BoltError::NETWORK_ERROR, "No valid connection for operation: " + operation_context};
            // Use connection's logger if available, otherwise driver's logger
            std::shared_ptr<spdlog::logger> log_to_use = (connection_ && connection_->get_logger()) ? connection_->get_logger() : drv_logger;
            if (log_to_use) log_to_use->warn("[SessionOp {}] {}", (connection_ ? connection_->get_id() : 0), out_err_pair.second);
            return nullptr;
        }

        // Ask the connection if it's truly ready
        if (!connection_->is_ready_for_queries()) {
            out_err_pair = {connection_->get_last_error(), connection_->get_last_error_message()};
            if (out_err_pair.first == boltprotocol::BoltError::SUCCESS) {  // If is_ready_for_queries is false but last_error is SUCCESS, something is off
                out_err_pair = {boltprotocol::BoltError::NETWORK_ERROR, "Connection reported not ready for queries despite no specific error."};
            }
            std::string context_msg = operation_context + " (connection not ready: " + out_err_pair.second + ")";
            _invalidate_session_due_to_connection_error(out_err_pair.first, context_msg);
            if (connection_->get_logger()) connection_->get_logger()->warn("[SessionOp {}] {}", connection_->get_id(), context_msg);
            return nullptr;
        }

        connection_->mark_as_used();
        out_err_pair = {boltprotocol::BoltError::SUCCESS, ""};
        return connection_.get();
    }

    const std::vector<std::string>& SessionHandle::get_last_bookmarks() const {
        return current_bookmarks_;
    }

    void SessionHandle::update_bookmarks(const std::vector<std::string>& new_bookmarks) {
        if (is_closed_) return;
        current_bookmarks_ = new_bookmarks;
        std::shared_ptr<spdlog::logger> logger = (connection_ && connection_->get_logger()) ? connection_->get_logger() : (transport_manager_ ? transport_manager_->get_config().logger : nullptr);
        if (logger) {
            std::string bookmarks_str;
            for (size_t i = 0; i < new_bookmarks.size(); ++i) {
                bookmarks_str += new_bookmarks[i] + (i == new_bookmarks.size() - 1 ? "" : ", ");
            }
            logger->trace("[SessionLC {}] Bookmarks updated to: [{}]", (connection_ ? connection_->get_id() : 0), bookmarks_str);
        }
    }

}  // namespace neo4j_bolt_transport