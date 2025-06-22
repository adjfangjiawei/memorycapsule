// SqlDriver/Source/sql_database_connection.cpp
#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_connection_parameters.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"

namespace cpporm_sqldriver {

    // 辅助函数实现
    void SqlDatabase::updateLastErrorFromDriver() const {
        if (m_driver) {
            m_last_error = m_driver->lastError();
        } else {
            // 如果驱动本身是 nullptr，这是一个严重的内部问题
            m_last_error = SqlError(ErrorCategory::DriverInternal, "Internal driver instance is null.", "updateLastErrorFromDriver");
        }
    }

    // --- 连接管理 ---
    bool SqlDatabase::open(const ConnectionParameters& params) {
        if (!m_driver) {
            // m_last_error should already be set by constructor if driver was null initially
            // If it became null later (which shouldn't happen with unique_ptr ownership), set error.
            if (m_last_error.category() == ErrorCategory::NoError) {
                m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::open");
            }
            return false;
        }
        if (m_driver->isOpen()) {
            m_driver->close();  // 重新打开前先关闭
        }
        m_parameters = params;  // 更新存储的参数
        bool success = m_driver->open(m_parameters);
        updateLastErrorFromDriver();  // 从驱动获取错误状态
        if (success) {
            m_transaction_active = false;  // 新连接，事务未激活
        }
        return success;
    }

    bool SqlDatabase::open() {
        if (!m_driver) {
            if (m_last_error.category() == ErrorCategory::NoError) {
                m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::open");
            }
            return false;
        }
        // 使用当前存储的 m_parameters 打开
        if (m_parameters.empty() && !m_driver->isOpen()) {  // 如果没有参数且未打开
            m_last_error = SqlError(ErrorCategory::Connectivity, "Cannot open: connection parameters not set and not already open.", "SqlDatabase::open");
            return false;
        }
        // 传递 m_parameters 给自身重载的 open(const ConnectionParameters&)
        return open(m_parameters);
    }

    bool SqlDatabase::open(const std::string& user, const std::string& password) {
        if (!m_driver) {
            if (m_last_error.category() == ErrorCategory::NoError) {
                m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::open");
            }
            return false;
        }
        ConnectionParameters temp_params = m_parameters;  // 复制当前参数
        temp_params.setUserName(user);
        temp_params.setPassword(password);
        return open(temp_params);  // 使用修改后的参数调用 open
    }

    void SqlDatabase::close() {
        if (m_driver && m_driver->isOpen()) {
            m_driver->close();
            updateLastErrorFromDriver();
        }
        m_transaction_active = false;  // 关闭连接后事务不再激活
    }

    bool SqlDatabase::isOpen() const {
        return m_driver && m_driver->isOpen();
    }

    bool SqlDatabase::isValid() const {
        // 一个有效的 SqlDatabase 实例必须有一个有效的驱动
        return m_driver != nullptr;
    }

    bool SqlDatabase::ping(int timeout_seconds) {
        if (!isOpen()) {  // 使用 isOpen() 检查
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection is not open to ping.", "SqlDatabase::ping");
            return false;
        }
        // m_driver 此时必然非空
        bool success = m_driver->ping(timeout_seconds);
        updateLastErrorFromDriver();
        return success;
    }

    // --- 字符集 ---
    bool SqlDatabase::setClientCharset(const std::string& charsetName) {
        if (!m_driver) {  // 必须有驱动才能设置字符集
            m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::setClientCharset");
            return false;
        }
        // 字符集通常在连接打开后设置，或者作为连接前选项
        // 假设 ISqlDriver::setClientCharset 知道如何处理
        bool success = m_driver->setClientCharset(charsetName);
        updateLastErrorFromDriver();
        if (success) {
            m_parameters.setClientCharset(charsetName);  // 更新缓存的参数
        }
        return success;
    }

    std::string SqlDatabase::clientCharset() const {
        if (!m_driver) return "";  // 无驱动则无字符集信息
        if (isOpen()) {            // 如果已连接，从驱动获取实时信息
            return m_driver->clientCharset();
        } else {  // 如果未连接，从参数缓存中获取
            return m_parameters.clientCharset().value_or("");
        }
    }

}  // namespace cpporm_sqldriver