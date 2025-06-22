// SqlDriver/Source/sql_database.cpp
#include "sqldriver/sql_database.h"

#include <string>
#include <utility>  // For std::move
#include <vector>

#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_connection_parameters.h"
#include "sqldriver/sql_enums.h"
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_index.h"
#include "sqldriver/sql_record.h"
#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    // --- 构造函数和析构函数 ---
    SqlDatabase::SqlDatabase(const std::string& driverTypeName, const std::string& assignedConnectionName, std::unique_ptr<ISqlDriver> driverImplementation)
        : m_driver_type_name(driverTypeName),
          m_connection_name(assignedConnectionName),
          m_driver(std::move(driverImplementation)),
          m_parameters(),  // 默认构造
          m_last_error(),  // 默认构造
          m_transaction_active(false) {
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
            m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::open");
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
            m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::open");
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
            m_last_error = SqlError(ErrorCategory::DriverInternal, "Driver not loaded.", "SqlDatabase::open");
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

    // --- 驱动和元数据访问 ---
    ISqlDriver* SqlDatabase::driver() const {
        return m_driver.get();
    }

    std::string SqlDatabase::connectionName() const {
        return m_connection_name;
    }

    std::vector<std::string> SqlDatabase::tables(ISqlDriverNs::TableType type, const std::string& schemaFilter, const std::string& tableNameFilter) const {
        if (!isOpen() || !m_driver) {  // 增加对 m_driver 的检查
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::tables");
            return {};
        }
        auto result = m_driver->tables(type, schemaFilter, tableNameFilter);
        updateLastErrorFromDriver();
        return result;
    }

    std::vector<std::string> SqlDatabase::schemas(const std::string& schemaFilter) const {
        if (!isOpen() || !m_driver) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::schemas");
            return {};
        }
        auto result = m_driver->schemas(schemaFilter);
        updateLastErrorFromDriver();
        return result;
    }

    SqlRecord SqlDatabase::record(const std::string& tablename, const std::string& schema) const {
        if (!isOpen() || !m_driver) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::record");
            return SqlRecord();
        }
        auto result = m_driver->record(tablename, schema);
        updateLastErrorFromDriver();
        return result;
    }

    SqlIndex SqlDatabase::primaryIndex(const std::string& tablename, const std::string& schema) const {
        if (!isOpen() || !m_driver) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::primaryIndex");
            return SqlIndex();
        }
        auto result = m_driver->primaryIndex(tablename, schema);
        updateLastErrorFromDriver();
        return result;
    }

    std::vector<SqlIndex> SqlDatabase::indexes(const std::string& tablename, const std::string& schema) const {
        if (!isOpen() || !m_driver) {
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available.", "SqlDatabase::indexes");
            return {};
        }
        auto result = m_driver->indexes(tablename, schema);
        updateLastErrorFromDriver();
        return result;
    }

    // --- 特性支持和版本信息 ---
    bool SqlDatabase::hasFeature(Feature feature) const {
        if (!m_driver) return false;
        return m_driver->hasFeature(feature);
    }

    SqlValue SqlDatabase::nativeHandle() const {
        if (!isOpen() || !m_driver) return SqlValue();
        return m_driver->nativeHandle();
    }

    std::string SqlDatabase::databaseProductVersion() const {
        if (!isOpen() || !m_driver) return "";
        return m_driver->databaseProductVersion();
    }

    std::string SqlDatabase::driverVersion() const {
        if (!m_driver) return "";  // 如果没有驱动，返回空
        return m_driver->driverVersion();
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

    // --- 序列 ---
    SqlValue SqlDatabase::nextSequenceValue(const std::string& sequenceName, const std::string& schema) {
        if (!isOpen() || !m_driver) {  // 增加对 m_driver 的检查
            m_last_error = SqlError(ErrorCategory::Connectivity, "Connection not open or driver not available for nextSequenceValue.", "SqlDatabase::nextSequenceValue");
            return SqlValue();
        }
        SqlValue val = m_driver->nextSequenceValue(sequenceName, schema);
        updateLastErrorFromDriver();
        return val;
    }

}  // namespace cpporm_sqldriver