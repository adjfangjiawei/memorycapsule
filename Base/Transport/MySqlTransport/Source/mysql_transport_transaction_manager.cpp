#include "cpporm_mysql_transport/mysql_transport_transaction_manager.h"

#include <vector>  // For parsing result of isolation level query

#include "cpporm_mysql_transport/mysql_transport_connection.h"  // For executeSimpleQuery, getLastError, etc.
#include "cpporm_mysql_transport/mysql_transport_result.h"      // For getTransactionIsolation querying server
#include "cpporm_mysql_transport/mysql_transport_statement.h"   // For getTransactionIsolation querying server

namespace cpporm_mysql_transport {

    MySqlTransportTransactionManager::MySqlTransportTransactionManager(MySqlTransportConnection* connection_context) : m_conn_ctx(connection_context), m_cached_isolation_level(TransactionIsolationLevel::None) {
        if (!m_conn_ctx) {
            // Programming error
            // throw std::invalid_argument("MySqlTransportTransactionManager: connection_context cannot be null.");
        }
    }

    bool MySqlTransportTransactionManager::executeSimpleQueryOnConnection(const std::string& query, const std::string& context_message) {
        if (!m_conn_ctx) return false;  // Should not happen if constructed properly
        // Delegate to a public method on MySqlTransportConnection that executes a simple query
        // and sets its own error state.
        // MySqlTransportConnection must have a method like:
        // bool _internalExecuteSimpleQuery(const std::string& query, const std::string& context_message);
        return m_conn_ctx->_internalExecuteSimpleQuery(query, context_message);
    }

    bool MySqlTransportTransactionManager::beginTransaction() {
        return executeSimpleQueryOnConnection("START TRANSACTION", "Failed to start transaction");
    }

    bool MySqlTransportTransactionManager::commit() {
        return executeSimpleQueryOnConnection("COMMIT", "Failed to commit transaction");
    }

    bool MySqlTransportTransactionManager::rollback() {
        return executeSimpleQueryOnConnection("ROLLBACK", "Failed to rollback transaction");
    }

    bool MySqlTransportTransactionManager::setTransactionIsolation(TransactionIsolationLevel level) {
        if (!m_conn_ctx || !m_conn_ctx->isConnected()) {
            if (m_conn_ctx) m_conn_ctx->setErrorManually(MySqlTransportError::Category::ConnectionError, "Not connected to set transaction isolation.");
            return false;
        }
        std::string sql;
        switch (level) {
            case TransactionIsolationLevel::ReadUncommitted:
                sql = "SET SESSION TRANSACTION ISOLATION LEVEL READ UNCOMMITTED";
                break;
            case TransactionIsolationLevel::ReadCommitted:
                sql = "SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED";
                break;
            case TransactionIsolationLevel::RepeatableRead:
                sql = "SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ";
                break;
            case TransactionIsolationLevel::Serializable:
                sql = "SET SESSION TRANSACTION ISOLATION LEVEL SERIALIZABLE";
                break;
            case TransactionIsolationLevel::None:
                if (m_conn_ctx) m_conn_ctx->setErrorManually(MySqlTransportError::Category::ApiUsageError, "Cannot set isolation level to 'None'.");
                return false;
        }

        if (executeSimpleQueryOnConnection(sql, "Failed to set transaction isolation level")) {
            m_cached_isolation_level = level;
            return true;
        }
        return false;
    }

    std::optional<TransactionIsolationLevel> MySqlTransportTransactionManager::getTransactionIsolation() const {
        if (!m_conn_ctx || !m_conn_ctx->isConnected()) {
            // Don't set error in const method, caller should check connection status
            return std::nullopt;
        }
        // Return cached if available and valid (not None, unless None is what was explicitly set somehow)
        if (m_cached_isolation_level != TransactionIsolationLevel::None) {
            return m_cached_isolation_level;
        }

        // Query from server: SELECT @@SESSION.transaction_isolation; (or @@SESSION.tx_isolation for older MySQL)
        std::string query_str = "SELECT @@SESSION.transaction_isolation";
        std::unique_ptr<MySqlTransportStatement> stmt = m_conn_ctx->createStatement(query_str);
        if (!stmt) {
            // Error already set by createStatement or connection
            return std::nullopt;
        }
        std::unique_ptr<MySqlTransportResult> result = stmt->executeQuery();
        if (!result || !result->isValid()) {
            // Error already set by executeQuery or result processing
            return std::nullopt;
        }

        if (result->fetchNextRow()) {
            auto val_opt = result->getValue(0);
            if (val_opt && val_opt.value().get_if<std::string>()) {
                std::string level_str = *val_opt.value().get_if<std::string>();
                // MySQL returns levels like "REPEATABLE-READ", "READ-COMMITTED", etc.
                std::transform(level_str.begin(), level_str.end(), level_str.begin(), [](unsigned char c) {
                    return std::toupper(c);
                });

                if (level_str == "READ-UNCOMMITTED") return TransactionIsolationLevel::ReadUncommitted;
                if (level_str == "READ-COMMITTED") return TransactionIsolationLevel::ReadCommitted;
                if (level_str == "REPEATABLE-READ") return TransactionIsolationLevel::RepeatableRead;
                if (level_str == "SERIALIZABLE") return TransactionIsolationLevel::Serializable;
            }
        }
        // If query fails or result is unexpected, return nullopt. Error should be on connection/statement.
        return std::nullopt;
    }

    void MySqlTransportTransactionManager::updateCachedIsolationLevel(TransactionIsolationLevel level) {
        m_cached_isolation_level = level;
    }

    bool MySqlTransportTransactionManager::setSavepoint(const std::string& name) {
        if (name.empty() || name.find_first_of("`'\" ") != std::string::npos) {
            if (m_conn_ctx) m_conn_ctx->setErrorManually(MySqlTransportError::Category::ApiUsageError, "Invalid savepoint name.");
            return false;
        }
        return executeSimpleQueryOnConnection("SAVEPOINT `" + m_conn_ctx->escapeString(name, false) + "`", "Failed to set savepoint " + name);
    }

    bool MySqlTransportTransactionManager::rollbackToSavepoint(const std::string& name) {
        if (name.empty() || name.find_first_of("`'\" ") != std::string::npos) {
            if (m_conn_ctx) m_conn_ctx->setErrorManually(MySqlTransportError::Category::ApiUsageError, "Invalid savepoint name for rollback.");
            return false;
        }
        return executeSimpleQueryOnConnection("ROLLBACK TO SAVEPOINT `" + m_conn_ctx->escapeString(name, false) + "`", "Failed to rollback to savepoint " + name);
    }

    bool MySqlTransportTransactionManager::releaseSavepoint(const std::string& name) {
        if (name.empty() || name.find_first_of("`'\" ") != std::string::npos) {
            if (m_conn_ctx) m_conn_ctx->setErrorManually(MySqlTransportError::Category::ApiUsageError, "Invalid savepoint name for release.");
            return false;
        }
        return executeSimpleQueryOnConnection("RELEASE SAVEPOINT `" + m_conn_ctx->escapeString(name, false) + "`", "Failed to release savepoint " + name);
    }

}  // namespace cpporm_mysql_transport