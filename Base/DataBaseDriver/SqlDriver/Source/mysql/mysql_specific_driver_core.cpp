// SqlDriver/Source/mysql/mysql_specific_driver_core.cpp
#include <stdexcept>  // For std::runtime_error

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_metadata.h"  // 包含 MySqlTransportMetadata 的完整定义
#include "cpporm_mysql_transport/mysql_transport_types.h"     // For MySqlTransportConnectionParams etc.
#include "sqldriver/mysql/mysql_driver_helper.h"
#include "sqldriver/mysql/mysql_specific_driver.h"
#include "sqldriver/mysql/mysql_specific_result.h"
#include "sqldriver/sql_driver_manager.h"  // Needed for registration

namespace cpporm_sqldriver {

    MySqlSpecificDriver::MySqlSpecificDriver()
        : m_transport_connection(nullptr),
          m_transport_metadata(nullptr),  // 初始化为 nullptr
          m_open_error_flag(false) {
        try {
            m_transport_connection = std::make_unique<cpporm_mysql_transport::MySqlTransportConnection>();
        } catch (const std::exception& e) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Failed to initialize MySqlTransportConnection: " + std::string(e.what()), "MySqlSpecificDriver constructor");
            m_open_error_flag = true;
        }
        // m_transport_metadata 将在 open() 成功时创建
    }

    MySqlSpecificDriver::~MySqlSpecificDriver() {
        // 直接执行 close() 的逻辑，避免从析构函数调用虚函数
        if (m_transport_connection && m_transport_connection->isConnected()) {
            m_transport_connection->disconnect();
        }
        m_transport_metadata.reset();    // 确保 MySqlTransportMetadata 在连接之前销毁（如果它依赖连接）
        m_transport_connection.reset();  // 销毁 transport connection
    }

    // const 方法现在可以安全地调用 getTransportConnection()->getLastError()
    // 并通过 mysql_helper 转换后更新 mutable m_last_error_cache
    void MySqlSpecificDriver::updateLastErrorCacheFromTransport(bool success_of_operation) const {
        if (m_transport_connection) {
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_connection->getLastError());
            if (!success_of_operation && m_last_error_cache.category() == ErrorCategory::NoError) {
                // 如果操作明确失败，但 transport 层没有报告错误，则这是一个驱动内部问题
                m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Operation reported failure, but transport layer shows no specific error.", "MySqlSpecificDriver");
            }
        } else {
            // 如果 transport_connection 本身就是 null，且上一个错误是 NoError，则更新
            if (m_last_error_cache.category() == ErrorCategory::NoError) {
                m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Transport connection is not available.", "MySqlSpecificDriver");
            }
        }
    }

    std::string MySqlSpecificDriver::resolveSchemaName(const std::string& schema_filter_from_args) const {
        if (!schema_filter_from_args.empty()) {
            return schema_filter_from_args;
        }
        // 从缓存的连接参数中获取数据库名
        if (auto dbName = m_current_params_cache.dbName()) {
            return *dbName;
        }
        return "";  // 如果都为空，则返回空字符串
    }

    bool MySqlSpecificDriver::open(const ConnectionParameters& params) {
        if (!m_transport_connection) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Transport connection not initialized.", "open");
            m_open_error_flag = true;
            return false;
        }

        if (isOpen()) {  // 使用 isOpen() 而不是直接访问 m_is_connected
            close();     // 调用自身的 close 方法
        }

        m_open_error_flag = false;        // 重置打开错误标记
        m_last_error_cache = SqlError();  // 清除之前的错误
        m_current_params_cache = params;  // 缓存参数

        ::cpporm_mysql_transport::MySqlTransportConnectionParams transport_params = mysql_helper::toMySqlTransportParams(params);
        bool success = m_transport_connection->connect(transport_params);
        updateLastErrorCacheFromTransport(success);  // 从 transport 层更新错误

        if (success) {
            // 连接成功后，初始化元数据提供者
            try {
                m_transport_metadata = std::make_unique<cpporm_mysql_transport::MySqlTransportMetadata>(m_transport_connection.get());
                if (!m_transport_metadata) {  // 确保创建成功
                    throw std::runtime_error("MySqlTransportMetadata could not be created.");
                }
            } catch (const std::exception& e) {
                if (m_transport_connection->isConnected()) m_transport_connection->disconnect();  // 创建元数据失败，则断开连接
                m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Failed to initialize metadata provider: " + std::string(e.what()), "open");
                success = false;  // 标记为失败
            }
        }

        if (!success) {
            m_open_error_flag = true;      // 标记打开操作失败
            m_transport_metadata.reset();  // 如果元数据创建失败或连接失败，则重置
        }
        return success;
    }

    void MySqlSpecificDriver::close() {
        bool had_error_before_close = m_open_error_flag || (m_last_error_cache.category() != ErrorCategory::NoError);

        if (m_transport_connection && m_transport_connection->isConnected()) {
            m_transport_connection->disconnect();
            // 不需要显式调用 updateLastErrorCacheFromTransport，因为 disconnect 通常不应该产生新错误
            // 如果 disconnect 自身可能失败并设置错误，则需要处理
        }
        m_transport_metadata.reset();  // 清除元数据对象

        // 如果关闭前没有错误，则清除错误状态
        if (!had_error_before_close) {
            m_last_error_cache = SqlError();
            m_open_error_flag = false;
        }
        // 如果关闭前有错误，则保留该错误状态
    }

    bool MySqlSpecificDriver::isOpen() const {
        return m_transport_connection && m_transport_connection->isConnected();
    }

    bool MySqlSpecificDriver::isOpenError() const {
        // 如果未打开，并且 m_open_error_flag 为 true，则表示上次打开失败
        return !isOpen() && m_open_error_flag;
    }

    bool MySqlSpecificDriver::ping(int timeout_seconds) {
        if (!isOpen()) {
            m_last_error_cache = SqlError(ErrorCategory::Connectivity, "Connection is not open for ping.", "ping");
            return false;
        }
        std::optional<unsigned int> transport_timeout;
        if (timeout_seconds >= 0) {
            transport_timeout = static_cast<unsigned int>(timeout_seconds);
        }
        bool success = m_transport_connection->ping(transport_timeout);
        updateLastErrorCacheFromTransport(success);
        return success;
    }

    std::unique_ptr<SqlResult> MySqlSpecificDriver::createResult() const {
        return std::make_unique<MySqlSpecificResult>(this);
    }

    SqlError MySqlSpecificDriver::lastError() const {
        return m_last_error_cache;
    }

    SqlValue MySqlSpecificDriver::nativeHandle() const {
        if (m_transport_connection && m_transport_connection->getNativeHandle()) {
            // MYSQL* 是一个不透明指针，将其转换为 void* 存储在 std::any 中
            return SqlValue::fromStdAny(std::any(static_cast<void*>(m_transport_connection->getNativeHandle())));
        }
        return SqlValue();  // 返回一个表示 NULL 的 SqlValue
    }

    cpporm_mysql_transport::MySqlTransportConnection* MySqlSpecificDriver::getTransportConnection() const {
        return m_transport_connection.get();
    }

    // 驱动初始化函数定义
    void MySqlDriver_Initialize() {
        SqlDriverManager::registerDriver("MYSQL",  // This is the key used in DbConfig.driver_type
                                         []() -> std::unique_ptr<ISqlDriver> {
                                             return std::make_unique<MySqlSpecificDriver>();
                                         });
    }

}  // namespace cpporm_sqldriver