// SqlDriver/Source/sql_database_connection.cpp
#include "sqldriver/i_sql_driver.h"  // For ISqlDriver methods
#include "sqldriver/sql_connection_parameters.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"

namespace cpporm_sqldriver {

    // Helper function implementation (if not already in a common place or header)
    void SqlDatabase::updateLastErrorFromDriver() const {
        if (m_driver) {
            m_last_error = m_driver->lastError();
        } else {
            // If driver is null, it's an internal error with SqlDatabase setup
            m_last_error = SqlError(ErrorCategory::DriverInternal, "Internal driver instance is null.", "updateLastErrorFromDriver");
        }
    }

    // --- Connection Management ---
    bool SqlDatabase::open(const ConnectionParameters& params) {
        if (!m_driver) {
            if (m_last_error.category() == ErrorCategory::NoError) {  // Only set if no prior error from constructor
                m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::open");
            }
            return false;
        }
        if (m_driver->isOpen()) {
            m_driver->close();  // Close before re-opening with new/current parameters
        }
        m_parameters = params;  // Store/update the parameters for this open attempt
        bool success = m_driver->open(m_parameters);
        updateLastErrorFromDriver();
        // m_transaction_active was removed; transaction state is now on the driver
        return success;
    }

    bool SqlDatabase::open() {
        if (!m_driver) {
            if (m_last_error.category() == ErrorCategory::NoError) {
                m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::open");
            }
            return false;
        }
        // Use current stored m_parameters.
        // If m_parameters is empty and driver is not already open, this might use driver defaults or fail.
        if (m_parameters.empty() && !m_driver->isOpen()) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Cannot open: connection parameters not set and not already open.", "SqlDatabase::open");
            return false;
        }
        // Calls the overload: open(const ConnectionParameters&)
        return open(m_parameters);
    }

    bool SqlDatabase::open(const std::string& user, const std::string& password) {
        if (!m_driver) {
            if (m_last_error.category() == ErrorCategory::NoError) {
                m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::open");
            }
            return false;
        }
        ConnectionParameters temp_params = m_parameters;  // Copy current parameters
        temp_params.setUserName(user);
        temp_params.setPassword(password);
        return open(temp_params);  // Call overload with modified parameters
    }

    void SqlDatabase::close() {
        if (m_driver && m_driver->isOpen()) {
            m_driver->close();
            updateLastErrorFromDriver();  // Get any error that occurred during close
        }
        // m_transaction_active was removed
    }

    bool SqlDatabase::isOpen() const {
        return m_driver && m_driver->isOpen();
    }

    bool SqlDatabase::isValid() const {
        // A valid SqlDatabase must have a non-null (shared) ISqlDriver instance
        return m_driver != nullptr;
    }

    bool SqlDatabase::ping(int timeout_seconds) {
        if (!isOpen()) {  // Use the public isOpen() which checks m_driver && m_driver->isOpen()
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection is not open to ping.", "SqlDatabase::ping");
            return false;
        }
        // m_driver is guaranteed to be non-null here due to isOpen() check
        bool success = m_driver->ping(timeout_seconds);
        updateLastErrorFromDriver();
        return success;
    }

    // --- Charset ---
    bool SqlDatabase::setClientCharset(const std::string& charsetName) {
        if (!m_driver) {
            m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::setClientCharset");
            return false;
        }
        // ISqlDriver::setClientCharset should handle whether it's pre- or post-connection.
        bool success = m_driver->setClientCharset(charsetName);
        updateLastErrorFromDriver();
        if (success) {
            m_parameters.setClientCharset(charsetName);  // Update cached parameters
        }
        return success;
    }

    std::string SqlDatabase::clientCharset() const {
        if (!m_driver) return "";
        // If connected, query the driver. Otherwise, return cached parameter.
        if (isOpen()) {
            return m_driver->clientCharset();
        } else {
            return m_parameters.clientCharset().value_or("");
        }
    }

}  // namespace cpporm_sqldriver