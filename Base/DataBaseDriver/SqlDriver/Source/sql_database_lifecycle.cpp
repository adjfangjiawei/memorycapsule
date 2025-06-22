// SqlDriver/Source/sql_database_lifecycle.cpp
#include <utility>  // For std::move

#include "sqldriver/i_sql_driver.h"  // For ISqlDriver methods like isOpen, close
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"  // For SqlError

namespace cpporm_sqldriver {

    // --- 构造函数和析构函数 ---
    SqlDatabase::SqlDatabase(const std::string& driverTypeName, const std::string& assignedConnectionName, std::unique_ptr<ISqlDriver> driverImplementation)
        : m_driver_type_name(driverTypeName),
          m_connection_name(assignedConnectionName),
          m_driver(std::move(driverImplementation)),
          m_parameters(),  // 默认构造
          m_last_error(),  // 默认构造
          m_transaction_active(false) {
        if (!m_driver) {                                                                                                  // If m_driver is null after move (meaning driverImplementation was null)
            m_last_error = SqlError(ErrorCategory::DriverInternal,                                                        // Or Connectivity/FeatureNotSupported
                                    "Driver for type '" + m_driver_type_name + "' not loaded or failed to instantiate.",  // databaseText
                                    "SqlDatabase initialization",                                                         // driverText
                                    "",
                                    0,                                    // native error code / state
                                    "Connection: " + m_connection_name);  // failedQuery (as context)
        }
    }

    SqlDatabase::~SqlDatabase() {
        if (m_driver && m_driver->isOpen()) {
            m_driver->close();
        }
    }

    // --- 移动语义 ---
    SqlDatabase::SqlDatabase(SqlDatabase&& other) noexcept
        : m_driver_type_name(std::move(other.m_driver_type_name)),
          m_connection_name(std::move(other.m_connection_name)),
          m_driver(std::move(other.m_driver)),
          m_parameters(std::move(other.m_parameters)),
          m_last_error(std::move(other.m_last_error)),  // SqlError 也应该支持移动
          m_transaction_active(other.m_transaction_active) {
        other.m_transaction_active = false;  // 重置被移动对象的事务状态
    }

    SqlDatabase& SqlDatabase::operator=(SqlDatabase&& other) noexcept {
        if (this != &other) {
            // 在移动赋值前，确保当前实例管理的资源被正确释放
            if (m_driver && m_driver->isOpen()) {
                m_driver->close();
            }
            m_driver_type_name = std::move(other.m_driver_type_name);
            m_connection_name = std::move(other.m_connection_name);
            m_driver = std::move(other.m_driver);
            m_parameters = std::move(other.m_parameters);
            m_last_error = std::move(other.m_last_error);
            m_transaction_active = other.m_transaction_active;

            other.m_transaction_active = false;  // 重置被移动对象的事务状态
        }
        return *this;
    }

}  // namespace cpporm_sqldriver