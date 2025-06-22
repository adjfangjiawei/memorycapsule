// SqlDriver/Source/sql_database_lifecycle.cpp
#include <utility>  // For std::move

#include "sqldriver/i_sql_driver.h"  // For ISqlDriver methods
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"  // For SqlError

namespace cpporm_sqldriver {

    // Constructor for SqlDriverManager
    SqlDatabase::SqlDatabase(const std::string& driverTypeName,
                             const std::string& assignedConnectionName,
                             std::shared_ptr<ISqlDriver> driverImplementation)  // Changed to shared_ptr
        : m_driver_type_name(driverTypeName),
          m_connection_name(assignedConnectionName),
          m_driver(std::move(driverImplementation)),  // Take ownership of the shared_ptr
          m_parameters(),
          m_last_error()
    // m_transaction_active removed
    {
        if (!m_driver) {
            m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver for type '" + m_driver_type_name + "' not loaded or failed to instantiate.", "SqlDatabase initialization", "", 0, "Connection: " + m_connection_name);
        }
    }

    SqlDatabase::~SqlDatabase() {
        // The shared_ptr m_driver will manage the ISqlDriver's lifetime.
        // If this is the last SqlDatabase object holding a reference to the driver,
        // and the driver is open, its destructor (or ISqlDriver's close) should handle closing.
        // Explicitly calling close() here might be redundant or interfere if other shared owners exist.
        // However, if a SqlDatabase instance itself logically "closes" its view of the connection:
        // if (m_driver && m_driver->isOpen()) {
        //    m_driver->close(); // This would affect all sharers.
        // }
        // For simplicity now, let shared_ptr handle ISqlDriver destruction, which should call its close.
    }

    // Copy constructor
    SqlDatabase::SqlDatabase(const SqlDatabase& other)
        : m_driver_type_name(other.m_driver_type_name),
          m_connection_name(other.m_connection_name),
          m_driver(other.m_driver),  // Copy the shared_ptr (increments ref count)
          m_parameters(other.m_parameters),
          m_last_error(other.m_last_error)
    // m_transaction_active removed
    {
    }

    // Copy assignment
    SqlDatabase& SqlDatabase::operator=(const SqlDatabase& other) {
        if (this != &other) {
            m_driver_type_name = other.m_driver_type_name;
            m_connection_name = other.m_connection_name;
            m_driver = other.m_driver;  // Assign shared_ptr
            m_parameters = other.m_parameters;
            m_last_error = other.m_last_error;
            // m_transaction_active removed
        }
        return *this;
    }

    // Move constructor
    SqlDatabase::SqlDatabase(SqlDatabase&& other) noexcept
        : m_driver_type_name(std::move(other.m_driver_type_name)),
          m_connection_name(std::move(other.m_connection_name)),
          m_driver(std::move(other.m_driver)),  // Move shared_ptr
          m_parameters(std::move(other.m_parameters)),
          m_last_error(std::move(other.m_last_error))
    // m_transaction_active removed
    {
    }

    // Move assignment
    SqlDatabase& SqlDatabase::operator=(SqlDatabase&& other) noexcept {
        if (this != &other) {
            m_driver_type_name = std::move(other.m_driver_type_name);
            m_connection_name = std::move(other.m_connection_name);
            m_driver = std::move(other.m_driver);  // Move shared_ptr
            m_parameters = std::move(other.m_parameters);
            m_last_error = std::move(other.m_last_error);
            // m_transaction_active removed
        }
        return *this;
    }

}  // namespace cpporm_sqldriver