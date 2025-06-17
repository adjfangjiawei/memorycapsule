#include <iostream>  // 调试用
#include <utility>   // For std::move

#include "neo4j_bolt_transport/config/session_parameters.h"          // For config::SessionParameters
#include "neo4j_bolt_transport/error/neo4j_error_util.h"             // For error formatting
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"  // For internal::BoltPhysicalConnection
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"               // For Neo4jBoltTransport access
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    SessionHandle::SessionHandle(Neo4jBoltTransport* transport_mgr, internal::BoltPhysicalConnection::PooledConnection conn_ptr, config::SessionParameters params_val)
        : transport_manager_(transport_mgr), connection_(std::move(conn_ptr)), session_params_(std::move(params_val)), current_bookmarks_(session_params_.initial_bookmarks) {  // 从会话参数初始化书签

        std::shared_ptr<spdlog::logger> drv_logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {  // 安全访问 logger
            drv_logger = transport_manager_->get_config().logger;
        }

        if (!transport_manager_) {
            connection_is_valid_ = false;
            is_closed_ = true;
            if (drv_logger) drv_logger->error("[SessionLC] SessionHandle created without a valid transport manager.");
            // connection_ 此时为 nullptr，_release_connection_to_pool 不会做任何事
            return;
        }

        std::shared_ptr<spdlog::logger> conn_logger = nullptr;
        if (connection_ && connection_->get_logger()) {
            conn_logger = connection_->get_logger();
        } else if (drv_logger) {
            conn_logger = drv_logger;  // 后备
        }

        if (!connection_ || !connection_->is_ready_for_queries()) {
            boltprotocol::BoltError last_err = connection_ ? connection_->get_last_error_code() : boltprotocol::BoltError::NETWORK_ERROR;
            std::string last_err_msg = connection_ ? connection_->get_last_error_message() : "Connection pointer null or not ready at SessionHandle construction.";

            if (conn_logger) conn_logger->warn("[SessionLC {}] Connection not ready at SessionHandle construction. Error: {}, Msg: {}", connection_ ? connection_->get_id() : 0, static_cast<int>(last_err), last_err_msg);
            _invalidate_session_due_to_connection_error(last_err, "SessionHandle construction: " + last_err_msg);
            _release_connection_to_pool(false);  // 释放可能坏掉的连接
        } else {
            connection_->mark_as_used();
            if (conn_logger) conn_logger->debug("[SessionLC {}] SessionHandle constructed with ready connection.", connection_->get_id());
        }
    }

    SessionHandle::~SessionHandle() {
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (connection_ && connection_->get_logger())
            logger = connection_->get_logger();
        else if (transport_manager_ && transport_manager_->get_config().logger)
            logger = transport_manager_->get_config().logger;

        if (logger) logger->debug("[SessionLC {}] SessionHandle destructing. Closed: {}, InTx: {}", (connection_ ? connection_->get_id() : 0), is_closed_, in_explicit_transaction_);
        close();  // 确保所有资源都被正确关闭和释放
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
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (connection_ && connection_->get_logger())
            logger = connection_->get_logger();
        else if (transport_manager_ && transport_manager_->get_config().logger)
            logger = transport_manager_->get_config().logger;

        if (logger) logger->trace("[SessionLC {}] SessionHandle move constructed from old SessionHandle.", (connection_ ? connection_->get_id() : 0));

        other.transport_manager_ = nullptr;  // other 现在无效
        other.is_closed_ = true;
        other.connection_is_valid_ = false;
    }

    SessionHandle& SessionHandle::operator=(SessionHandle&& other) noexcept {
        if (this != &other) {
            std::shared_ptr<spdlog::logger> logger = nullptr;
            if (connection_ && connection_->get_logger())
                logger = connection_->get_logger();
            else if (transport_manager_ && transport_manager_->get_config().logger)
                logger = transport_manager_->get_config().logger;
            if (logger) logger->trace("[SessionLC {}] SessionHandle move assigning from other SessionHandle.", (connection_ ? connection_->get_id() : 0));

            close();  // 首先关闭当前会话

            transport_manager_ = other.transport_manager_;
            connection_ = std::move(other.connection_);
            session_params_ = std::move(other.session_params_);
            in_explicit_transaction_ = other.in_explicit_transaction_;
            current_transaction_query_id_ = other.current_transaction_query_id_;
            current_bookmarks_ = std::move(other.current_bookmarks_);
            is_closed_ = other.is_closed_;
            connection_is_valid_ = other.connection_is_valid_;

            other.transport_manager_ = nullptr;  // other 现在无效
            other.is_closed_ = true;
            other.connection_is_valid_ = false;
        }
        return *this;
    }

    void SessionHandle::_release_connection_to_pool(bool mark_healthy) {
        if (connection_ && transport_manager_) {
            std::shared_ptr<spdlog::logger> logger = connection_->get_logger();  // 优先使用连接的logger
            uint64_t conn_id = connection_->get_id();
            if (logger) logger->trace("[SessionLC conn_id={}] Releasing connection to pool. Healthy: {}", conn_id, mark_healthy && connection_is_valid_);
            transport_manager_->release_connection(std::move(connection_), mark_healthy && connection_is_valid_);
            // connection_ 现在为 nullptr
        }
        connection_is_valid_ = false;  // 释放后，会话不再拥有有效连接
    }

    void SessionHandle::close() {
        if (is_closed_) {
            return;
        }

        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (connection_ && connection_->get_logger())
            logger = connection_->get_logger();
        else if (transport_manager_ && transport_manager_->get_config().logger)
            logger = transport_manager_->get_config().logger;

        if (logger) logger->debug("[SessionLC {}] Closing SessionHandle. InTx: {}", (connection_ ? connection_->get_id() : 0), in_explicit_transaction_);

        if (in_explicit_transaction_ && connection_is_valid_ && connection_ && connection_->is_ready_for_queries()) {
            if (logger) logger->info("[SessionLC {}] Rolling back active transaction during close.", connection_->get_id());
            rollback_transaction();  // 这会将 in_explicit_transaction_ 设为 false
        }
        _release_connection_to_pool(connection_is_valid_);  // 根据当前连接的有效性状态释放
        is_closed_ = true;
    }

    void SessionHandle::_invalidate_session_due_to_connection_error(boltprotocol::BoltError error, const std::string& context_message) {
        connection_is_valid_ = false;
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (connection_ && connection_->get_logger())
            logger = connection_->get_logger();
        else if (transport_manager_ && transport_manager_->get_config().logger)
            logger = transport_manager_->get_config().logger;

        if (logger) {
            logger->warn("[SessionLC {}] Session invalidated due to connection error. Code: {} ({}), Context: {}",
                         (connection_ ? connection_->get_id() : 0),
                         static_cast<int>(error),
                         error::bolt_error_to_string(error),  // 添加错误码的字符串表示
                         context_message);
        }
    }

    internal::BoltPhysicalConnection* SessionHandle::_get_valid_connection_for_operation(std::pair<boltprotocol::BoltError, std::string>& out_err_pair, const std::string& operation_context) {
        std::shared_ptr<spdlog::logger> drv_logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            drv_logger = transport_manager_->get_config().logger;
        }

        if (is_closed_) {
            out_err_pair = {boltprotocol::BoltError::INVALID_ARGUMENT, "Operation on closed session: " + operation_context};
            if (drv_logger) drv_logger->warn("[SessionOp] {}", out_err_pair.second);
            return nullptr;
        }
        if (!connection_is_valid_ || !connection_) {
            out_err_pair = {boltprotocol::BoltError::NETWORK_ERROR, "No valid connection for operation: " + operation_context};
            std::shared_ptr<spdlog::logger> log_to_use = (connection_ && connection_->get_logger()) ? connection_->get_logger() : drv_logger;
            if (log_to_use) log_to_use->warn("[SessionOp conn_id={}] {}", (connection_ ? connection_->get_id() : 0), out_err_pair.second);
            return nullptr;
        }

        if (!connection_->is_ready_for_queries()) {
            out_err_pair = {connection_->get_last_error_code(), connection_->get_last_error_message()};
            if (out_err_pair.first == boltprotocol::BoltError::SUCCESS) {  // 如果 is_ready 为 false 但上次错误是 SUCCESS，则有问题
                out_err_pair = {boltprotocol::BoltError::NETWORK_ERROR, "Connection reported not ready for queries despite no specific error."};
            }
            std::string context_msg_full = operation_context + " (connection not ready: " + out_err_pair.second + ")";
            _invalidate_session_due_to_connection_error(out_err_pair.first, context_msg_full);  // 传递更详细的上下文
            if (connection_->get_logger()) connection_->get_logger()->warn("[SessionOp conn_id={}] {}", connection_->get_id(), context_msg_full);
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

        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (connection_ && connection_->get_logger())
            logger = connection_->get_logger();
        else if (transport_manager_ && transport_manager_->get_config().logger)
            logger = transport_manager_->get_config().logger;

        if (logger) {
            std::string bookmarks_str;
            if (new_bookmarks.empty()) {
                bookmarks_str = "<empty>";
            } else {
                for (size_t i = 0; i < new_bookmarks.size(); ++i) {
                    bookmarks_str += new_bookmarks[i] + (i == new_bookmarks.size() - 1 ? "" : ", ");
                }
            }
            logger->trace("[SessionLC {}] Bookmarks updated to: [{}]", (connection_ ? connection_->get_id() : 0), bookmarks_str);
        }
    }

}  // namespace neo4j_bolt_transport