// cpporm_mysql_transport/mysql_transport_connection_core.cpp
#include <mysql/mysql.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_charset_handler.h"
#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_connection_options_setter.h"
#include "cpporm_mysql_transport/mysql_transport_server_info_provider.h"
#include "cpporm_mysql_transport/mysql_transport_transaction_manager.h"

namespace cpporm_mysql_transport {

    static std::atomic<int> g_mysql_library_init_count(0);
    static std::mutex g_mysql_library_mutex;

    void ensure_mysql_library_initialized() {
        std::lock_guard<std::mutex> lock(g_mysql_library_mutex);
        if (g_mysql_library_init_count.fetch_add(1, std::memory_order_relaxed) == 0) {
            if (mysql_library_init(0, nullptr, nullptr)) {
                g_mysql_library_init_count.fetch_sub(1, std::memory_order_relaxed);
                throw std::runtime_error("Failed to initialize MySQL C library");
            }
        }
    }

    void try_mysql_library_end() {
        std::lock_guard<std::mutex> lock(g_mysql_library_mutex);
        if (g_mysql_library_init_count.load(std::memory_order_relaxed) > 0) {
            if (g_mysql_library_init_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
                mysql_library_end();
            }
        }
    }

    MySqlTransportConnection::MySqlTransportConnection() : m_mysql_handle(nullptr), m_is_connected(false), m_current_isolation_level(TransactionIsolationLevel::None) {
        ensure_mysql_library_initialized();
        m_mysql_handle = mysql_init(nullptr);
        if (!m_mysql_handle) {
            m_last_error = MySqlTransportError(MySqlTransportError::Category::ResourceError, "mysql_init() failed (out of memory?)");
        }
        m_options_setter = std::make_unique<MySqlTransportConnectionOptionsSetter>(this);
        m_transaction_manager = std::make_unique<MySqlTransportTransactionManager>(this);
        m_charset_handler = std::make_unique<MySqlTransportCharsetHandler>(this);
        m_server_info_provider = std::make_unique<MySqlTransportServerInfoProvider>();
    }

    MySqlTransportConnection::~MySqlTransportConnection() {
        disconnect();
        if (m_mysql_handle) {
            mysql_close(m_mysql_handle);
            m_mysql_handle = nullptr;
        }
        try_mysql_library_end();
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

        if (m_options_setter) m_options_setter->m_conn_ctx = this;
        if (m_transaction_manager) m_transaction_manager->m_conn_ctx = this;
        if (m_charset_handler) m_charset_handler->m_conn_ctx = this;
    }

    MySqlTransportConnection& MySqlTransportConnection::operator=(MySqlTransportConnection&& other) noexcept {
        if (this != &other) {
            disconnect();
            if (m_mysql_handle) {
                mysql_close(m_mysql_handle);
            }

            m_mysql_handle = other.m_mysql_handle;
            m_is_connected = other.m_is_connected;
            m_current_params = std::move(other.m_current_params);
            m_last_error = std::move(other.m_last_error);
            m_current_isolation_level = other.m_current_isolation_level;
            m_options_setter = std::move(other.m_options_setter);
            m_transaction_manager = std::move(other.m_transaction_manager);
            m_charset_handler = std::move(other.m_charset_handler);
            m_server_info_provider = std::move(other.m_server_info_provider);

            other.m_mysql_handle = nullptr;
            other.m_is_connected = false;

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
        if (!m_mysql_handle) {
            setErrorManually(MySqlTransportError::Category::InternalError, "MySQL handle is null before connect (mysql_init failed?).");
            return false;
        }
        if (!m_options_setter) {
            setErrorManually(MySqlTransportError::Category::InternalError, "Connection options setter not initialized.");
            return false;
        }

        clearError();
        m_current_params = params;

        if (!m_options_setter->applyPreConnectOptions(m_mysql_handle, params)) {
            if (m_last_error.isOk()) {
                setErrorFromMySqlHandle(m_mysql_handle, "Failed to apply pre-connect options");
            }
            return false;
        }

        const char* host_ptr = params.host.empty() ? nullptr : params.host.c_str();
        const char* user_ptr = params.user.empty() ? nullptr : params.user.c_str();
        const char* passwd_ptr = params.password.empty() ? nullptr : params.password.c_str();
        const char* db_ptr = params.db_name.empty() ? nullptr : params.db_name.c_str();
        unsigned int port_val = params.port == 0 ? 3306 : params.port;
        const char* unix_socket_ptr = params.unix_socket.empty() ? nullptr : params.unix_socket.c_str();

        if (!mysql_real_connect(m_mysql_handle, host_ptr, user_ptr, passwd_ptr, db_ptr, port_val, unix_socket_ptr, params.client_flag)) {
            setErrorFromMySqlHandle(m_mysql_handle, "mysql_real_connect failed");
            return false;
        }

        m_is_connected = true;

        if (params.charset.has_value() && !params.charset.value().empty()) {
            if (!setClientCharset(params.charset.value())) {
                disconnect();
                return false;
            }
        }

        for (const auto& pair : params.init_commands) {
            std::string command = pair.first;
            if (!pair.second.empty()) {
                command = pair.first + "=" + pair.second;
            }
            if (!_internalExecuteSimpleQuery(command, "Failed to execute init command: " + pair.first)) {
                disconnect();
                return false;
            }
        }

        if (m_transaction_manager) {
            auto initial_level_opt = m_transaction_manager->getTransactionIsolation();
            if (initial_level_opt) {
                m_current_isolation_level = *initial_level_opt;
                m_transaction_manager->updateCachedIsolationLevel(*initial_level_opt);
            } else {
                if (!getLastError().isOk()) {
                }
                m_current_isolation_level = TransactionIsolationLevel::None;
                m_transaction_manager->updateCachedIsolationLevel(TransactionIsolationLevel::None);
            }
        }
        return true;
    }

    void MySqlTransportConnection::disconnect() {
        m_is_connected = false;
        m_current_isolation_level = TransactionIsolationLevel::None;
        if (m_transaction_manager) {
            m_transaction_manager->updateCachedIsolationLevel(TransactionIsolationLevel::None);
        }
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
        if (mysql_ping(m_mysql_handle) != 0) {
            setErrorFromMySqlHandle(m_mysql_handle, "mysql_ping failed (connection may be down)");
            m_is_connected = false;
            return false;
        }
        return true;
    }

    // getCurrentParams() is defined inline in the header, remove from .cpp
    // const MySqlTransportConnectionParams& MySqlTransportConnection::getCurrentParams() const {
    //     return m_current_params;
    // }

    MySqlTransportError MySqlTransportConnection::getLastError() const {
        return m_last_error;
    }

    void MySqlTransportConnection::clearError() {
        m_last_error = MySqlTransportError();
    }

    void MySqlTransportConnection::setErrorFromMySqlHandle(MYSQL* handle_to_check_error_on, const std::string& context_message) {
        if (handle_to_check_error_on) {
            unsigned int err_no = mysql_errno(handle_to_check_error_on);
            if (err_no != 0) {
                const char* sqlstate = mysql_sqlstate(handle_to_check_error_on);
                const char* errmsg = mysql_error(handle_to_check_error_on);
                std::string full_msg = context_message;
                if (errmsg && errmsg[0] != '\0') {
                    if (!full_msg.empty()) full_msg += ": ";
                    full_msg += errmsg;
                }
                m_last_error = MySqlTransportError(MySqlTransportError::Category::QueryError, full_msg, static_cast<int>(err_no), sqlstate, errmsg);
            } else if (!context_message.empty() && m_last_error.isOk()) {
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
        unsigned int err_no_opt = 0;
        const char* sql_state_opt = nullptr;
        const char* err_msg_opt = nullptr;

        if (m_mysql_handle) {
            err_no_opt = mysql_errno(m_mysql_handle);
            if (err_no_opt != 0) {
                sql_state_opt = mysql_sqlstate(m_mysql_handle);
            }
        }
        m_last_error = MySqlTransportError(MySqlTransportError::Category::ConnectionError, "Pre-connect option failure: " + option_error_message, static_cast<int>(err_no_opt), sql_state_opt, err_msg_opt);
    }

}  // namespace cpporm_mysql_transport