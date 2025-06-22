// SqlDriver/Source/sql_database_transaction.cpp
#include "sqldriver/i_sql_driver.h"  // For ISqlDriver methods
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_enums.h"  // For TransactionIsolationLevel
#include "sqldriver/sql_error.h"

namespace cpporm_sqldriver {

    // --- Transaction Management ---
    bool SqlDatabase::transaction() {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open for transaction.", "SqlDatabase::transaction");
            return false;
        }
        // ISqlDriver itself might track active transaction, or we query.
        // For simplicity, if driver's beginTransaction succeeds, we assume active.
        // If driver allows querying active state, that's more robust.
        if (m_driver->hasFeature(Feature::Transactions) && m_driver->beginTransaction()) {
            updateLastErrorFromDriver();  // Update error even on success if driver sets warnings
            return true;
        }
        updateLastErrorFromDriver();
        if (m_last_error.category() == ErrorCategory::NoError && m_driver->hasFeature(Feature::Transactions)) {
            // If beginTransaction returned false but no error was set by driver,
            // it might mean transaction was already active or feature not fully supported in context.
            m_last_error = SqlError(ErrorCategory::Transaction, "beginTransaction call returned false without specific driver error.", "SqlDatabase::transaction");
        } else if (!m_driver->hasFeature(Feature::Transactions)) {
            m_last_error = SqlError(ErrorCategory::FeatureNotSupported, "Transactions not supported by driver.", "SqlDatabase::transaction");
        }
        return false;
    }

    bool SqlDatabase::commit() {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open for commit.", "SqlDatabase::commit");
            return false;
        }
        // Query driver for active transaction before attempting commit
        // This requires ISqlDriver to have an isTransactionActive() method.
        // For now, assume commitTransaction will fail if no tx active.
        if (!m_driver->hasFeature(Feature::Transactions)) {
            m_last_error = SqlError(ErrorCategory::FeatureNotSupported, "Transactions not supported by driver.", "SqlDatabase::commit");
            return false;
        }
        bool success = m_driver->commitTransaction();
        updateLastErrorFromDriver();
        return success;
    }

    bool SqlDatabase::rollback() {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open for rollback.", "SqlDatabase::rollback");
            return false;
        }
        if (!m_driver->hasFeature(Feature::Transactions)) {
            m_last_error = SqlError(ErrorCategory::FeatureNotSupported, "Transactions not supported by driver.", "SqlDatabase::rollback");
            return false;
        }
        bool success = m_driver->rollbackTransaction();
        updateLastErrorFromDriver();
        return success;
    }

    bool SqlDatabase::isTransactionActive() const {
        // This method now directly queries the driver if possible.
        // Requires ISqlDriver to have a method like `isTransactionActive() const`.
        // If ISqlDriver doesn't have it, SqlDatabase cannot reliably know without its own tracking.
        // Assuming ISqlDriver provides this:
        // if (m_driver && m_driver->isOpen() && m_driver->hasFeature(Feature::Transactions)) {
        //     return m_driver->isTransactionActive(); // Hypothetical ISqlDriver method
        // }
        // For now, if m_transaction_active was removed, we can't implement this accurately
        // without adding the method to ISqlDriver.
        // A fallback (less accurate): return true if beginTransaction was called and no commit/rollback since.
        // But SqlDatabase no longer tracks m_transaction_active.
        // This method now reflects the underlying C-API state if driver provides it.
        // For MySQL, `mysql_get_server_status(m_mysql_handle) & SERVER_STATUS_IN_TRANS`
        // We need to abstract this into ISqlDriver.
        // For now, this is a placeholder, as ISqlDriver does not have isTransactionActive().
        // In a real scenario, the driver implementation (e.g., MySqlSpecificDriver)
        // would query its native handle. SqlDatabase would call that.
        if (m_driver && m_driver->isOpen() && m_driver->hasFeature(Feature::Transactions)) {
            // Simulate querying the driver (needs actual ISqlDriver method)
            // This is a stub. Actual implementation would be in the specific driver.
            // For example, MySqlSpecificDriver would check MYSQL status.
            // For simplicity here, we can't determine it perfectly.
            // The `is_explicit_transaction_handle_` in Session becomes more important.
        }
        return false;  // Placeholder: cannot determine without ISqlDriver::isTransactionActive()
    }

    bool SqlDatabase::setTransactionIsolationLevel(TransactionIsolationLevel level) {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open to set isolation level.", "SqlDatabase::setTransactionIsolationLevel");
            return false;
        }
        bool success = m_driver->setTransactionIsolationLevel(level);
        updateLastErrorFromDriver();
        return success;
    }

    TransactionIsolationLevel SqlDatabase::transactionIsolationLevel() const {
        if (!isOpen() || !m_driver) {
            return TransactionIsolationLevel::Default;
        }
        return m_driver->transactionIsolationLevel();
    }

    bool SqlDatabase::setSavepoint(const std::string& name) {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Transaction, "Connection not open or no active transaction for setSavepoint.", "SqlDatabase::setSavepoint");
            return false;
        }
        bool success = m_driver->setSavepoint(name);
        updateLastErrorFromDriver();
        return success;
    }

    bool SqlDatabase::rollbackToSavepoint(const std::string& name) {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Transaction, "Connection not open or no active transaction for rollbackToSavepoint.", "SqlDatabase::rollbackToSavepoint");
            return false;
        }
        bool success = m_driver->rollbackToSavepoint(name);
        updateLastErrorFromDriver();
        return success;
    }

    bool SqlDatabase::releaseSavepoint(const std::string& name) {
        if (!isOpen()) {
            m_last_error = SqlError(ErrorCategory::Transaction, "Connection not open or no active transaction for releaseSavepoint.", "SqlDatabase::releaseSavepoint");
            return false;
        }
        bool success = m_driver->releaseSavepoint(name);
        updateLastErrorFromDriver();
        return success;
    }

}  // namespace cpporm_sqldriver