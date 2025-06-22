// MySqlTransport/Source/mysql_transport_connection_core.cpp
#include <mysql/mysql.h>

#include <atomic>
#include <mutex>
#include <stdexcept>  // 用于 std::runtime_error

#include "cpporm_mysql_transport/mysql_transport_connection.h"  // 主头文件

// 组件头文件，因为 MySqlTransportConnection 会创建它们
#include "cpporm_mysql_transport/mysql_transport_charset_handler.h"
#include "cpporm_mysql_transport/mysql_transport_connection_options_setter.h"
#include "cpporm_mysql_transport/mysql_transport_server_info_provider.h"
#include "cpporm_mysql_transport/mysql_transport_transaction_manager.h"

namespace cpporm_mysql_transport {

    // 用于一次性初始化的更简单的全局状态
    static std::atomic<bool> g_mysql_library_actually_initialized_flag(false);
    static std::mutex g_mysql_library_init_call_mutex;  // 保护对 mysql_library_init 的首次调用

    void ensure_mysql_library_initialized() {
        // 快速路径：如果已经初始化，直接返回。
        if (g_mysql_library_actually_initialized_flag.load(std::memory_order_acquire)) {
            return;
        }

        // 慢速路径：可能是首次调用，需要加锁
        std::lock_guard<std::mutex> lock(g_mysql_library_init_call_mutex);
        // 获取锁后再次检查
        if (g_mysql_library_actually_initialized_flag.load(std::memory_order_relaxed)) {
            return;
        }

        if (mysql_library_init(0, nullptr, nullptr)) {
            // 这是致命错误，无法在此处递减 g_mysql_library_init_count。
            throw std::runtime_error("Failed to initialize MySQL C library (call from ensure_mysql_library_initialized)");
        }
        g_mysql_library_actually_initialized_flag.store(true, std::memory_order_release);
    }

    void try_mysql_library_end() {
        // 在新的策略下，这是一个空操作。
        // mysql_library_end() 将不会被调用。
        // 操作系统将在进程退出时进行清理。
    }

    MySqlTransportConnection::MySqlTransportConnection()
        : m_mysql_handle(nullptr),
          m_is_connected(false),
          m_current_isolation_level(TransactionIsolationLevel::None)  // 初始化成员
    {
        ensure_mysql_library_initialized();  // 调用修订后的函数
        m_mysql_handle = mysql_init(nullptr);
        if (!m_mysql_handle) {
            // 直接设置错误或抛出异常，确保 m_last_error 被设置
            m_last_error = MySqlTransportError(MySqlTransportError::Category::ResourceError, "mysql_init() failed in MySqlTransportConnection constructor");
            // 可以考虑抛出 std::runtime_error("mysql_init failed");
        }
        // 初始化 PImpl 成员 (组件)
        m_options_setter = std::make_unique<MySqlTransportConnectionOptionsSetter>(this);
        m_transaction_manager = std::make_unique<MySqlTransportTransactionManager>(this);
        m_charset_handler = std::make_unique<MySqlTransportCharsetHandler>(this);
        m_server_info_provider = std::make_unique<MySqlTransportServerInfoProvider>();
    }

    MySqlTransportConnection::~MySqlTransportConnection() {
        disconnect();  // 确保逻辑断开
        if (m_mysql_handle) {
            mysql_close(m_mysql_handle);
            m_mysql_handle = nullptr;
        }
        try_mysql_library_end();  // 调用修订后的 (现在是空操作的) 函数
    }

    MySqlTransportConnection::MySqlTransportConnection(MySqlTransportConnection&& other) noexcept
        : m_mysql_handle(other.m_mysql_handle),
          m_is_connected(other.m_is_connected),
          m_current_params(std::move(other.m_current_params)),
          m_last_error(std::move(other.m_last_error)),
          m_current_isolation_level(other.m_current_isolation_level),
          m_options_setter(std::move(other.m_options_setter)),
          m_transaction_manager(std::move(other.m_transaction_manager)),
          m_charset_handler(std::move(other.m_charset_handler)),
          m_server_info_provider(std::move(other.m_server_info_provider)) {
        other.m_mysql_handle = nullptr;
        other.m_is_connected = false;

        // 重要：更新被移动的组件中的上下文指针为 'this'
        if (m_options_setter) m_options_setter->m_conn_ctx = this;
        if (m_transaction_manager) m_transaction_manager->m_conn_ctx = this;
        if (m_charset_handler) m_charset_handler->m_conn_ctx = this;
        // m_server_info_provider 不存储 m_conn_ctx
    }

    MySqlTransportConnection& MySqlTransportConnection::operator=(MySqlTransportConnection&& other) noexcept {
        if (this != &other) {
            // 正确清理当前状态
            disconnect();
            if (m_mysql_handle) {
                mysql_close(m_mysql_handle);
            }
            // 此处不需要调用 try_mysql_library_end()，因为库是全局管理的

            // 从 other 移动资源
            m_mysql_handle = other.m_mysql_handle;
            m_is_connected = other.m_is_connected;
            m_current_params = std::move(other.m_current_params);
            m_last_error = std::move(other.m_last_error);
            m_current_isolation_level = other.m_current_isolation_level;
            m_options_setter = std::move(other.m_options_setter);
            m_transaction_manager = std::move(other.m_transaction_manager);
            m_charset_handler = std::move(other.m_charset_handler);
            m_server_info_provider = std::move(other.m_server_info_provider);

            // 重置 other
            other.m_mysql_handle = nullptr;
            other.m_is_connected = false;
            // other 的 unique_ptrs (m_options_setter 等) 现在是 nullptr

            // 更新被移动的组件中的上下文指针为 'this'
            if (m_options_setter) m_options_setter->m_conn_ctx = this;
            if (m_transaction_manager) m_transaction_manager->m_conn_ctx = this;
            if (m_charset_handler) m_charset_handler->m_conn_ctx = this;
        }
        return *this;
    }

    bool MySqlTransportConnection::connect(const MySqlTransportConnectionParams& params) {
        if (m_is_connected) {
            setErrorManually(MySqlTransportError::Category::ConnectionError, "Already connected. Disconnect first.");
            return false;
        }
        if (!m_mysql_handle) {  // 这个检查应该在构造函数的 mysql_init 之后
            setErrorManually(MySqlTransportError::Category::InternalError, "MySQL handle is null before connect (mysql_init failed?).");
            return false;
        }
        if (!m_options_setter) {  // 应该在构造函数中初始化
            setErrorManually(MySqlTransportError::Category::InternalError, "Connection options setter not initialized.");
            return false;
        }

        clearError();
        m_current_params = params;

        if (!m_options_setter->applyPreConnectOptions(m_mysql_handle, params)) {
            // m_options_setter.applyPreConnectOptions 应该已经在这个连接上下文中设置了错误
            if (m_last_error.isOk()) {  // 防御性：如果它没有设置，设置一个通用的
                setErrorFromMySqlHandle(m_mysql_handle, "Failed to apply pre-connect options");
            }
            return false;
        }

        const char* host_ptr = params.host.empty() ? nullptr : params.host.c_str();
        const char* user_ptr = params.user.empty() ? nullptr : params.user.c_str();
        const char* passwd_ptr = params.password.empty() ? nullptr : params.password.c_str();
        const char* db_ptr = params.db_name.empty() ? nullptr : params.db_name.c_str();
        // mysql_real_connect 对 port 0 的处理是使用默认端口
        unsigned int port_val = params.port;
        const char* unix_socket_ptr = params.unix_socket.empty() ? nullptr : params.unix_socket.c_str();

        if (!mysql_real_connect(m_mysql_handle, host_ptr, user_ptr, passwd_ptr, db_ptr, port_val, unix_socket_ptr, params.client_flag)) {
            setErrorFromMySqlHandle(m_mysql_handle, "mysql_real_connect failed");
            return false;
        }

        m_is_connected = true;  // 在需要连接的进一步操作之前设置连接状态

        // 连接后设置字符集（如果已指定）
        if (params.charset.has_value() && !params.charset.value().empty()) {
            if (!setClientCharset(params.charset.value())) {  // 这个方法使用 m_charset_handler
                // 错误会由 setClientCharset 设置
                disconnect();  // 回滚连接尝试
                return false;
            }
        }

        // 执行初始化命令
        for (const auto& pair : params.init_commands) {
            std::string command = pair.first;  // command key 本身可能是命令
            if (!pair.second.empty()) {        // 如果提供了 value，则假定是 key=value 格式的 SET
                command = pair.first + "=" + pair.second;
            }
            if (!_internalExecuteSimpleQuery(command, "Failed to execute init command: " + pair.first)) {
                disconnect();
                return false;
            }
        }

        // 缓存初始隔离级别
        if (m_transaction_manager) {
            auto initial_level_opt = m_transaction_manager->getTransactionIsolation();
            if (initial_level_opt.has_value()) {
                m_current_isolation_level = initial_level_opt.value();
                m_transaction_manager->updateCachedIsolationLevel(initial_level_opt.value());
            } else {
                // 如果获取隔离级别失败，m_last_error 上应该有错误
                // 缓存默认为 None
                m_current_isolation_level = TransactionIsolationLevel::None;
                m_transaction_manager->updateCachedIsolationLevel(TransactionIsolationLevel::None);
            }
        }
        return true;
    }

    void MySqlTransportConnection::disconnect() {
        // 此处不调用 mysql_close。mysql_close 在析构函数中。
        // Disconnect 仅标记逻辑状态并重置缓存。
        m_is_connected = false;
        m_current_isolation_level = TransactionIsolationLevel::None;
        if (m_transaction_manager) {  // 检查 PImpl 成员是否有效
            m_transaction_manager->updateCachedIsolationLevel(TransactionIsolationLevel::None);
        }
        // 注意：如果存在活动事务，它将保留在服务器上，
        // 除非在断开连接前显式回滚。mysql_close 会隐式回滚。
    }

    bool MySqlTransportConnection::isConnected() const {
        return m_is_connected && m_mysql_handle;
    }

    bool MySqlTransportConnection::ping(std::optional<unsigned int> /*timeout_seconds*/) {
        if (!isConnected()) {
            setErrorManually(MySqlTransportError::Category::ConnectionError, "Not connected to server for ping.");
            return false;
        }
        clearError();
        // timeout_seconds 不被 mysql_ping 直接支持。
        // 它使用句柄上设置的连接/读/写超时。
        if (mysql_ping(m_mysql_handle) != 0) {
            setErrorFromMySqlHandle(m_mysql_handle, "mysql_ping failed (connection may be down)");
            m_is_connected = false;  // 假设连接丢失
            return false;
        }
        return true;
    }

    MySqlTransportError MySqlTransportConnection::getLastError() const {
        return m_last_error;
    }

    void MySqlTransportConnection::clearError() {
        m_last_error = MySqlTransportError();
    }

    void MySqlTransportConnection::setErrorFromMySqlHandle(MYSQL* handle_to_check_error_on, const std::string& context_message) {
        if (handle_to_check_error_on) {
            unsigned int err_no = mysql_errno(handle_to_check_error_on);
            // 仅当 err_no 非零时才设置错误，或者如果 context_message很重要且之前没有错误。
            if (err_no != 0) {
                const char* sqlstate = mysql_sqlstate(handle_to_check_error_on);
                const char* errmsg = mysql_error(handle_to_check_error_on);
                std::string full_msg = context_message;
                if (errmsg && errmsg[0] != '\0') {
                    if (!full_msg.empty()) full_msg += ": ";
                    full_msg += errmsg;
                }
                // 如果可能，根据 err_no 或 sqlstate 确定类别
                MySqlTransportError::Category cat = MySqlTransportError::Category::QueryError;                               // 查询/语句错误的默认类别
                if (err_no >= CR_MIN_ERROR && err_no <= CR_MAX_ERROR) cat = MySqlTransportError::Category::ConnectionError;  // 客户端错误通常是连接错误
                // 如果需要，添加更精细的类别映射

                m_last_error = MySqlTransportError(cat, full_msg, static_cast<int>(err_no), sqlstate, errmsg);
            } else if (!context_message.empty() && m_last_error.isOk()) {
                // 如果 mysql_errno 为0，但我们有上下文消息且之前没有错误，
                // 这可能表示内部逻辑问题或非MySQL错误条件。
                m_last_error = MySqlTransportError(MySqlTransportError::Category::InternalError, context_message);
            }
        } else {
            m_last_error = MySqlTransportError(MySqlTransportError::Category::InternalError, context_message.empty() ? "MySQL handle is null" : context_message + ": MySQL handle is null");
        }
    }

    void MySqlTransportConnection::setErrorManually(MySqlTransportError::Category cat, const std::string& msg, int native_mysql_err, const char* native_sql_state, const char* native_mysql_msg, unsigned int proto_errc) {
        m_last_error = MySqlTransportError(cat, msg, native_mysql_err, native_sql_state, native_mysql_msg, proto_errc);
    }

    void MySqlTransportConnection::recordPreConnectOptionError(const std::string& option_error_message) {
        // 此方法通常由 MySqlTransportConnectionOptionsSetter 在 mysql_options() 失败时调用。
        // mysql_options() 不设置 mysql_errno()。
        // 因此，我们创建一个没有特定MySQL代码的错误，只有消息。
        m_last_error = MySqlTransportError(MySqlTransportError::Category::ConnectionError, "Pre-connect option failure: " + option_error_message);
    }

}  // namespace cpporm_mysql_transport