// SqlDriver/Source/sql_database_transaction.cpp
#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_enums.h"  // For TransactionIsolationLevel
#include "sqldriver/sql_error.h"

namespace cpporm_sqldriver {

    // --- 事务管理 ---
    bool SqlDatabase::transaction() {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open for transaction.", "SqlDatabase::transaction");
            return false;
        }
        if (m_transaction_active) {
            m_last_error = SqlError(ErrorCategory::Transaction, "Transaction already active.", "SqlDatabase::transaction");
            return false;  // 或者根据需要允许嵌套（如果驱动支持）
        }
        // m_driver 此时必然非空
        bool success = m_driver->beginTransaction();
        updateLastErrorFromDriver();
        if (success) {
            m_transaction_active = true;
        }
        return success;
    }

    bool SqlDatabase::commit() {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open for commit.", "SqlDatabase::commit");
            return false;
        }
        if (!m_transaction_active) {
            m_last_error = SqlError(ErrorCategory::Transaction, "No active transaction to commit.", "SqlDatabase::commit");
            return false;
        }
        // m_driver 此时必然非空
        bool success = m_driver->commitTransaction();
        updateLastErrorFromDriver();
        m_transaction_active = false;  // 提交后事务结束
        return success;
    }

    bool SqlDatabase::rollback() {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open for rollback.", "SqlDatabase::rollback");
            return false;
        }
        if (!m_transaction_active) {
            // 某些数据库允许在没有活动事务时执行ROLLBACK（通常是无操作或错误）
            // 为保持一致性，如果SqlDatabase认为没有活动事务，则报告错误
            m_last_error = SqlError(ErrorCategory::Transaction, "No active transaction to rollback.", "SqlDatabase::rollback");
            return false;
        }
        // m_driver 此时必然非空
        bool success = m_driver->rollbackTransaction();
        updateLastErrorFromDriver();
        m_transaction_active = false;  // 回滚后事务结束
        return success;
    }

    bool SqlDatabase::isTransactionActive() const {
        // 此状态由 SqlDatabase 自身跟踪，因为它调用 beginTransaction, commit, rollback
        // 如果需要从驱动层面查询真实状态，会更复杂且可能慢
        return isOpen() && m_transaction_active;
    }

    bool SqlDatabase::setTransactionIsolationLevel(TransactionIsolationLevel level) {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open to set isolation level.", "SqlDatabase::setTransactionIsolationLevel");
            return false;
        }
        // m_driver 此时必然非空
        bool success = m_driver->setTransactionIsolationLevel(level);
        updateLastErrorFromDriver();
        return success;
    }

    TransactionIsolationLevel SqlDatabase::transactionIsolationLevel() const {
        if (!isOpen() || !m_driver) {  // 增加对 m_driver 的检查
            return TransactionIsolationLevel::Default;
        }
        return m_driver->transactionIsolationLevel();
    }

    bool SqlDatabase::setSavepoint(const std::string& name) {
        if (!isOpen() || !isTransactionActive()) {
            m_last_error = SqlError(ErrorCategory::Transaction, "No active transaction or connection closed for setSavepoint.", "SqlDatabase::setSavepoint");
            return false;
        }
        // m_driver 此时必然非空
        bool success = m_driver->setSavepoint(name);
        updateLastErrorFromDriver();
        return success;
    }

    bool SqlDatabase::rollbackToSavepoint(const std::string& name) {
        if (!isOpen() || !isTransactionActive()) {
            m_last_error = SqlError(ErrorCategory::Transaction, "No active transaction or connection closed for rollbackToSavepoint.", "SqlDatabase::rollbackToSavepoint");
            return false;
        }
        bool success = m_driver->rollbackToSavepoint(name);
        updateLastErrorFromDriver();
        return success;
    }

    bool SqlDatabase::releaseSavepoint(const std::string& name) {
        if (!isOpen() || !isTransactionActive()) {
            m_last_error = SqlError(ErrorCategory::Transaction, "No active transaction or connection closed for releaseSavepoint.", "SqlDatabase::releaseSavepoint");
            return false;
        }
        bool success = m_driver->releaseSavepoint(name);
        updateLastErrorFromDriver();
        return success;
    }

}  // namespace cpporm_sqldriver