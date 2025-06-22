// cpporm_mysql_transport/mysql_transport_connection_transaction.cpp
#include "cpporm_mysql_transport/mysql_transport_connection.h"
// MySqlTransportTransactionManager is already included via mysql_transport_connection.h -> .cpp (core)
// or directly if needed for types:
#include "cpporm_mysql_transport/mysql_transport_transaction_manager.h"

namespace cpporm_mysql_transport {

    bool MySqlTransportConnection::beginTransaction() {
        if (!m_transaction_manager) {
            setErrorManually(MySqlTransportError::Category::InternalError, "Transaction manager not initialized.");
            return false;
        }
        return m_transaction_manager->beginTransaction();
    }

    bool MySqlTransportConnection::commit() {
        if (!m_transaction_manager) {
            setErrorManually(MySqlTransportError::Category::InternalError, "Transaction manager not initialized.");
            return false;
        }
        return m_transaction_manager->commit();
    }

    bool MySqlTransportConnection::rollback() {
        if (!m_transaction_manager) {
            setErrorManually(MySqlTransportError::Category::InternalError, "Transaction manager not initialized.");
            return false;
        }
        return m_transaction_manager->rollback();
    }

    bool MySqlTransportConnection::setTransactionIsolation(TransactionIsolationLevel level) {
        if (!m_transaction_manager) {
            setErrorManually(MySqlTransportError::Category::InternalError, "Transaction manager not initialized.");
            return false;
        }
        bool success = m_transaction_manager->setTransactionIsolation(level);
        if (success) {
            m_current_isolation_level = level;
        }
        return success;
    }

    std::optional<TransactionIsolationLevel> MySqlTransportConnection::getTransactionIsolation() const {
        if (!m_transaction_manager) {
            // Cannot set error in const method without mutable m_last_error
            return std::nullopt;
        }
        if (!isConnected() && m_current_isolation_level == TransactionIsolationLevel::None) {
            return std::nullopt;
        }
        if (m_current_isolation_level != TransactionIsolationLevel::None) {
            return m_current_isolation_level;
        }

        // If connected and cache is None, query the server.
        // The component m_transaction_manager might cache it or query live.
        // This const getter itself won't update m_current_isolation_level unless it's mutable.
        return m_transaction_manager->getTransactionIsolation();
    }

    bool MySqlTransportConnection::setSavepoint(const std::string& name) {
        if (!m_transaction_manager) {
            setErrorManually(MySqlTransportError::Category::InternalError, "Transaction manager not initialized.");
            return false;
        }
        return m_transaction_manager->setSavepoint(name);
    }

    bool MySqlTransportConnection::rollbackToSavepoint(const std::string& name) {
        if (!m_transaction_manager) {
            setErrorManually(MySqlTransportError::Category::InternalError, "Transaction manager not initialized.");
            return false;
        }
        return m_transaction_manager->rollbackToSavepoint(name);
    }

    bool MySqlTransportConnection::releaseSavepoint(const std::string& name) {
        if (!m_transaction_manager) {
            setErrorManually(MySqlTransportError::Category::InternalError, "Transaction manager not initialized.");
            return false;
        }
        return m_transaction_manager->releaseSavepoint(name);
    }

}  // namespace cpporm_mysql_transport