// SqlDriver/Source/sql_database_properties.cpp
#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_connection_parameters.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    // --- 连接属性 ---
    std::string SqlDatabase::driverName() const {
        return m_driver_type_name;
    }

    std::string SqlDatabase::databaseName() const {
        return m_parameters.dbName().value_or("");
    }

    void SqlDatabase::setDatabaseName(const std::string& name) {
        m_parameters.setDbName(name);
        // 如果已连接，某些驱动可能允许更改当前数据库，但这里只更新参数
    }

    std::string SqlDatabase::userName() const {
        return m_parameters.userName().value_or("");
    }

    void SqlDatabase::setUserName(const std::string& name) {
        m_parameters.setUserName(name);
    }

    std::string SqlDatabase::password() const {
        // 通常不直接返回密码，但如果API需要
        return m_parameters.password().value_or("");
    }

    void SqlDatabase::setPassword(const std::string& password) {
        m_parameters.setPassword(password);
    }

    std::string SqlDatabase::hostName() const {
        return m_parameters.hostName().value_or("");
    }

    void SqlDatabase::setHostName(const std::string& host) {
        m_parameters.setHostName(host);
    }

    int SqlDatabase::port() const {
        return m_parameters.port().value_or(-1);
    }

    void SqlDatabase::setPort(int port) {
        m_parameters.setPort(port);
    }

    std::string SqlDatabase::connectOptionsString() const {
        return m_parameters.connectOptions().value_or("");
    }

    void SqlDatabase::setConnectOptionsString(const std::string& options) {
        m_parameters.setConnectOptions(options);
    }

    const ConnectionParameters& SqlDatabase::connectionParameters() const {
        return m_parameters;
    }

    void SqlDatabase::setConnectionParameter(const std::string& key, const SqlValue& value) {
        m_parameters[key] = value;  // 直接使用 map 的 operator[]
    }

    SqlValue SqlDatabase::connectionParameter(const std::string& key) const {
        auto it = m_parameters.find(key);
        if (it != m_parameters.end()) {
            return it->second;
        }
        return SqlValue();  // 返回表示 NULL 的 SqlValue
    }

    SqlError SqlDatabase::lastError() const {
        return m_last_error;
    }

    ISqlDriver* SqlDatabase::driver() const {
        return m_driver.get();
    }

    std::string SqlDatabase::connectionName() const {
        return m_connection_name;
    }

}  // namespace cpporm_sqldriver