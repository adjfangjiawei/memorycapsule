// SqlDriver/Source/mysql/mysql_specific_driver_transaction.cpp
#include "cpporm_mysql_transport/mysql_transport_connection.h"  // For full type definition
#include "cpporm_mysql_transport/mysql_transport_types.h"       // For TransactionIsolationLevel enum from transport
#include "sqldriver/mysql/mysql_driver_helper.h"                // For enum converters
#include "sqldriver/mysql/mysql_specific_driver.h"

namespace cpporm_sqldriver {

    bool MySqlSpecificDriver::beginTransaction() {
        if (!isOpen()) {
            m_last_error_cache = SqlError(ErrorCategory::Connectivity, "Connection is not open.", "beginTransaction");
            return false;
        }
        // m_transport_connection is std::unique_ptr<MySqlTransportConnection>
        bool success = m_transport_connection->beginTransaction();
        updateLastErrorCacheFromTransport(success);  // updateLastErrorCacheFromTransport is now const
        return success;
    }

    bool MySqlSpecificDriver::commitTransaction() {
        if (!isOpen()) {
            m_last_error_cache = SqlError(ErrorCategory::Connectivity, "Connection is not open.", "commitTransaction");
            return false;
        }
        bool success = m_transport_connection->commit();
        updateLastErrorCacheFromTransport(success);
        return success;
    }

    bool MySqlSpecificDriver::rollbackTransaction() {
        if (!isOpen()) {
            m_last_error_cache = SqlError(ErrorCategory::Connectivity, "Connection is not open.", "rollbackTransaction");
            return false;
        }
        bool success = m_transport_connection->rollback();
        updateLastErrorCacheFromTransport(success);
        return success;
    }

    bool MySqlSpecificDriver::setTransactionIsolationLevel(TransactionIsolationLevel level) {
        if (!isOpen()) {
            m_last_error_cache = SqlError(ErrorCategory::Connectivity, "Connection is not open.", "setTransactionIsolationLevel");
            return false;
        }
        // Use fully qualified name for transport's enum
        ::cpporm_mysql_transport::TransactionIsolationLevel transport_level = mysql_helper::toMySqlTransportIsolationLevel(level);
        bool success = m_transport_connection->setTransactionIsolation(transport_level);
        updateLastErrorCacheFromTransport(success);
        return success;
    }

    TransactionIsolationLevel MySqlSpecificDriver::transactionIsolationLevel() const {
        if (!isOpen() || !m_transport_connection) {  // Added check for m_transport_connection
            return TransactionIsolationLevel::Default;
        }
        // Use fully qualified name for transport's enum
        std::optional<::cpporm_mysql_transport::TransactionIsolationLevel> transport_level_opt = m_transport_connection->getTransactionIsolation();
        if (transport_level_opt) {
            // If querying isolation level itself caused an error in transport, update cache
            // Note: getTransactionIsolation in transport might not set error if it just returns cached.
            // We assume if transport_level_opt is empty, transport layer has an error set.
            updateLastErrorCacheFromTransport(transport_level_opt.has_value());  // Update error based on success of getting level
            return mysql_helper::fromMySqlTransportIsolationLevel(transport_level_opt.value());
        } else {
            updateLastErrorCacheFromTransport(false);   // Getting isolation level failed
            return TransactionIsolationLevel::Default;  // Or some error indicator if possible
        }
    }

    bool MySqlSpecificDriver::setSavepoint(const std::string& name) {
        if (!isOpen()) {
            m_last_error_cache = SqlError(ErrorCategory::Connectivity, "Connection is not open.", "setSavepoint");
            return false;
        }
        bool success = m_transport_connection->setSavepoint(name);
        updateLastErrorCacheFromTransport(success);
        return success;
    }

    bool MySqlSpecificDriver::rollbackToSavepoint(const std::string& name) {
        if (!isOpen()) {
            m_last_error_cache = SqlError(ErrorCategory::Connectivity, "Connection is not open.", "rollbackToSavepoint");
            return false;
        }
        bool success = m_transport_connection->rollbackToSavepoint(name);
        updateLastErrorCacheFromTransport(success);
        return success;
    }

    bool MySqlSpecificDriver::releaseSavepoint(const std::string& name) {
        if (!isOpen()) {
            m_last_error_cache = SqlError(ErrorCategory::Connectivity, "Connection is not open.", "releaseSavepoint");
            return false;
        }
        bool success = m_transport_connection->releaseSavepoint(name);
        updateLastErrorCacheFromTransport(success);
        return success;
    }

}  // namespace cpporm_sqldriver