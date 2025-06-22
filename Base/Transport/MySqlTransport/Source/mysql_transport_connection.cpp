// cpporm_mysql_transport/mysql_transport_connection.cpp
#include "cpporm_mysql_transport/mysql_transport_connection.h"

#include <mysql/mysql.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_statement.h"
// #include "mysql_protocol/mysql_constants.h" // Not directly used here usually
// #include "mysql_protocol/mysql_type_converter.h" // Not directly used here usually

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
            // mysql_init rarely fails. If it does, it's critical (e.g. out of memory).
            // We can't use mysql_error() here as handle is null.
            m_last_error = MySqlTransportError(MySqlTransportError::Category::ResourceError, "mysql_init() failed (out of memory?)");
            // Optionally throw, as the object is unusable.
            // throw std::runtime_error("MySqlTransportConnection: mysql_init() failed.");
        }
        // Initialize components
        m_options_setter = std::make_unique<MySqlTransportConnectionOptionsSetter>(this);
        m_transaction_manager = std::make_unique<MySqlTransportTransactionManager>(this);
        m_charset_handler = std::make_unique<MySqlTransportCharsetHandler>(this);
        m_server_info_provider = std::make_unique<MySqlTransportServerInfoProvider>();  // Does not need 'this'
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

        // Update context in moved components
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

            // Update context in moved components
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
        if (!m_options_setter) {  // Should have been initialized
            setErrorManually(MySqlTransportError::Category::InternalError, "Connection options setter not initialized.");
            return false;
        }

        clearError();
        m_current_params = params;  // Store current params

        // Apply pre-connect options using the dedicated setter component
        if (!m_options_setter->applyPreConnectOptions(m_mysql_handle, params)) {
            // Error should already be set by m_options_setter via recordPreConnectOptionError
            // or by direct calls to setErrorFromMySqlHandle if it could use the handle.
            // If applyPreConnectOptions sets error on this connection context, we are good.
            if (m_last_error.isOk()) {  // If setter didn't set, provide a generic one
                setErrorFromMySqlHandle(m_mysql_handle, "Failed to apply pre-connect options");
            }
            return false;
        }

        const char* host_ptr = params.host.empty() ? nullptr : params.host.c_str();
        const char* user_ptr = params.user.empty() ? nullptr : params.user.c_str();
        const char* passwd_ptr = params.password.empty() ? nullptr : params.password.c_str();
        const char* db_ptr = params.db_name.empty() ? nullptr : params.db_name.c_str();
        unsigned int port_val = params.port == 0 ? 3306 : params.port;  // Default port if 0
        const char* unix_socket_ptr = params.unix_socket.empty() ? nullptr : params.unix_socket.c_str();

        if (!mysql_real_connect(m_mysql_handle, host_ptr, user_ptr, passwd_ptr, db_ptr, port_val, unix_socket_ptr, params.client_flag)) {
            setErrorFromMySqlHandle(m_mysql_handle, "mysql_real_connect failed");
            // Optional: attempt to re-init m_mysql_handle for future connect attempts
            // mysql_close(m_mysql_handle); m_mysql_handle = mysql_init(nullptr);
            return false;
        }

        m_is_connected = true;

        // Set client character set if specified (after connection)
        if (params.charset.has_value() && !params.charset.value().empty()) {
            if (!setClientCharset(params.charset.value())) {
                // Error already set by setClientCharset (via m_charset_handler)
                disconnect();  // Failed post-connection setup
                return false;
            }
        }

        // Execute init commands
        for (const auto& pair : params.init_commands) {
            // Values in init_commands might need escaping if they are strings.
            // Assuming simple key=value pairs for now, where value might be a quoted string already or numeric.
            // A more robust solution would parse the value.
            std::string command = pair.first;              // Example: "SET SESSION sql_mode='TRADITIONAL'"
            if (!pair.second.empty()) {                    // if value is provided
                command = pair.first + "=" + pair.second;  // Example: "sql_mode" = "'TRADITIONAL'"
            }

            if (!_internalExecuteSimpleQuery(command, "Failed to execute init command: " + pair.first)) {
                disconnect();
                return false;
            }
        }

        // After successful connection, determine and cache the initial transaction isolation level.
        // This makes getTransactionIsolation() faster if not changed.
        auto initial_level_opt = m_transaction_manager->getTransactionIsolation();  // Query server
        if (initial_level_opt) {
            m_current_isolation_level = *initial_level_opt;
            m_transaction_manager->updateCachedIsolationLevel(*initial_level_opt);
        } else {
            // If getTransactionIsolation failed, m_transaction_manager would have set an error on m_conn_ctx (this).
            // We should check that error and potentially fail the connection process.
            if (!getLastError().isOk()) {
                // An error occurred trying to get the initial isolation level.
                // Depending on policy, this could be a connection failure.
                // For now, log/ignore and proceed with None.
            }
            m_current_isolation_level = TransactionIsolationLevel::None;
            m_transaction_manager->updateCachedIsolationLevel(TransactionIsolationLevel::None);
        }

        return true;
    }

    void MySqlTransportConnection::disconnect() {
        if (m_is_connected) {
            // Resources managed by MYSQL* (like results of mysql_store_result not yet freed)
            // are typically cleaned up by mysql_close().
            // Prepared statements should be closed before disconnecting.
        }
        m_is_connected = false;
        // Don't nullify m_mysql_handle, destructor will mysql_close it.
        // Or, if disconnect means "can be reconnected later with same object":
        // mysql_close(m_mysql_handle); m_mysql_handle = mysql_init(nullptr);
        // For simplicity, assume ~MySqlTransportConnection is the main cleanup point for the handle.
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
        // mysql_ping does not accept a timeout.
        if (mysql_ping(m_mysql_handle) != 0) {
            setErrorFromMySqlHandle(m_mysql_handle, "mysql_ping failed (connection may be down)");
            m_is_connected = false;  // Assume connection is lost on ping failure
            return false;
        }
        return true;
    }

    std::unique_ptr<MySqlTransportStatement> MySqlTransportConnection::createStatement(const std::string& query) {
        if (!m_mysql_handle) {  // Main check is for a valid MYSQL object
                                // Let statement constructor handle null connection by setting its own error.
        }
        return std::make_unique<MySqlTransportStatement>(this, query);
    }

    bool MySqlTransportConnection::beginTransaction() {
        if (!m_transaction_manager) return false;
        return m_transaction_manager->beginTransaction();
    }
    bool MySqlTransportConnection::commit() {
        if (!m_transaction_manager) return false;
        return m_transaction_manager->commit();
    }
    bool MySqlTransportConnection::rollback() {
        if (!m_transaction_manager) return false;
        return m_transaction_manager->rollback();
    }
    bool MySqlTransportConnection::setTransactionIsolation(TransactionIsolationLevel level) {
        if (!m_transaction_manager) return false;
        bool success = m_transaction_manager->setTransactionIsolation(level);
        if (success) {
            m_current_isolation_level = level;  // Update local cache
        }
        return success;
    }
    std::optional<TransactionIsolationLevel> MySqlTransportConnection::getTransactionIsolation() const {
        if (!m_transaction_manager) return std::nullopt;
        // Prefer locally cached if valid, else query via manager (which might also cache or query server)
        // If connection is not active, getting isolation level from server is not possible.
        if (!isConnected() && m_current_isolation_level == TransactionIsolationLevel::None) {
            return std::nullopt;  // Cannot query server, and no valid cached value.
        }
        if (m_current_isolation_level != TransactionIsolationLevel::None) {
            // This can return the cached value even if not connected, if it was set prior.
            return m_current_isolation_level;
        }

        // If connected and cache is None, query the server.
        auto level_opt = m_transaction_manager->getTransactionIsolation();
        if (level_opt) {
            // const_cast is needed if we want to update cache in a const method.
            // This is generally bad practice. Let's make m_current_isolation_level mutable
            // or make this method non-const if we want to update cache here.
            // For now, let the manager handle its own caching, and this method updates its *own* cache
            // if it successfully queries. The current code already updates m_current_isolation_level
            // in connect() and setTransactionIsolation(). This const getter could update a mutable cache.
            // MySqlTransportConnection * me = const_cast<MySqlTransportConnection*>(this);
            // me->m_current_isolation_level = *level_opt;
            // me->m_transaction_manager->updateCachedIsolationLevel(*level_opt);
        }
        return level_opt;
    }
    bool MySqlTransportConnection::setSavepoint(const std::string& name) {
        if (!m_transaction_manager) return false;
        return m_transaction_manager->setSavepoint(name);
    }
    bool MySqlTransportConnection::rollbackToSavepoint(const std::string& name) {
        if (!m_transaction_manager) return false;
        return m_transaction_manager->rollbackToSavepoint(name);
    }
    bool MySqlTransportConnection::releaseSavepoint(const std::string& name) {
        if (!m_transaction_manager) return false;
        return m_transaction_manager->releaseSavepoint(name);
    }

    bool MySqlTransportConnection::setClientCharset(const std::string& charset_name) {
        if (!m_charset_handler) return false;
        bool success = m_charset_handler->setClientCharset(m_mysql_handle, charset_name, !m_is_connected);
        if (success && m_is_connected) {  // If connected and successful, update current params
            m_current_params.charset = charset_name;
        } else if (success && !m_is_connected) {  // If pre-connect and successful, update current params
            m_current_params.charset = charset_name;
        }
        return success;
    }

    std::optional<std::string> MySqlTransportConnection::getClientCharset() const {
        if (!m_charset_handler) return std::nullopt;

        if (m_is_connected && m_mysql_handle) {  // Only query live charset if connected
            auto live_charset = m_charset_handler->getClientCharset(m_mysql_handle, m_is_connected);
            if (live_charset) {
                // It's good practice to update the cached m_current_params.charset if it differs
                // but this is a const method. A mutable member or a non-const variant would be needed.
                // For now, just return what's live.
                return live_charset;
            }
        }

        // If not connected or handler couldn't get it from live handle, try stored params
        if (m_current_params.charset.has_value() && !m_current_params.charset.value().empty()) {
            return m_current_params.charset.value();
        }
        return std::nullopt;
    }

    std::string MySqlTransportConnection::getServerVersionString() const {
        if (!m_server_info_provider || !m_mysql_handle) return "Not available";
        return m_server_info_provider->getServerVersionString(m_mysql_handle);
    }
    unsigned long MySqlTransportConnection::getServerVersionNumber() const {
        if (!m_server_info_provider || !m_mysql_handle) return 0;
        return m_server_info_provider->getServerVersionNumber(m_mysql_handle);
    }
    std::string MySqlTransportConnection::getHostInfo() const {
        if (!m_server_info_provider || !m_mysql_handle) return "Not available";
        return m_server_info_provider->getHostInfo(m_mysql_handle, m_is_connected);
    }

    MySqlTransportError MySqlTransportConnection::getLastError() const {
        return m_last_error;
    }

    std::string MySqlTransportConnection::escapeString(const std::string& unescaped_str, bool /*treat_backslash_as_meta*/) {
        if (!m_mysql_handle) {
            // Cannot use setErrorManually directly here as it's non-const, and escapeString might be const.
            // For now, assume escapeString is non-const.
            setErrorManually(MySqlTransportError::Category::InternalError, "MySQL handle not available for escapeString.");
            return unescaped_str;  // Or throw
        }
        clearError();
        std::vector<char> to_buffer(unescaped_str.length() * 2 + 1);
        unsigned long to_length = mysql_real_escape_string(m_mysql_handle, to_buffer.data(), unescaped_str.c_str(), unescaped_str.length());
        return std::string(to_buffer.data(), to_length);
    }

    // --- Private/Internal Helper Methods ---
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
                m_last_error = MySqlTransportError(MySqlTransportError::Category::QueryError,  // Default, can be refined
                                                   full_msg,
                                                   err_no,
                                                   sqlstate,
                                                   errmsg);
            } else if (!context_message.empty() && m_last_error.isOk()) {
                // If mysql_errno is 0, but a context message implies an issue from the caller
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
        // This is called by ConnectionOptionsSetter when mysql_options fails.
        // mysql_error(m_mysql_handle) might not be meaningful before connect.
        // So, we set a more generic error based on the context from the setter.
        // It's possible mysql_options sets some internal error code retrievable by mysql_errno on the handle
        // even before connection, but this is less common.
        unsigned int err_no_opt = 0;
        const char* sql_state_opt = nullptr;
        // const char* err_msg_opt = nullptr; // Let's not use mysql_error() for pre-connect option error for now.

        if (m_mysql_handle) {  // Check if handle is valid enough to get errno
            err_no_opt = mysql_errno(m_mysql_handle);
            if (err_no_opt != 0) {  // If mysql_options set an error code
                sql_state_opt = mysql_sqlstate(m_mysql_handle);
                // err_msg_opt = mysql_error(m_mysql_handle); // This might be risky pre-connect
            }
        }
        // If err_no_opt is still 0, it means mysql_options failed without setting a standard MySQL error code.
        m_last_error = MySqlTransportError(MySqlTransportError::Category::ConnectionError,  // Or ApiUsageError
                                           "Pre-connect option failure: " + option_error_message,
                                           static_cast<int>(err_no_opt),  // Cast to int for MySqlTransportError
                                           sql_state_opt,
                                           nullptr /* err_msg_opt */);
    }

    bool MySqlTransportConnection::_internalExecuteSimpleQuery(const std::string& query, const std::string& context_message) {
        if (!isConnected()) {
            setErrorManually(MySqlTransportError::Category::ConnectionError, context_message.empty() ? "Not connected to server." : context_message + ": Not connected.");
            return false;
        }
        clearError();
        if (mysql_real_query(m_mysql_handle, query.c_str(), query.length()) != 0) {
            setErrorFromMySqlHandle(m_mysql_handle, context_message.empty() ? "Query failed" : context_message);
            return false;
        }

        // Consume any potential result set from simple queries (e.g., some SET commands, SHOW, etc.)
        // This loop handles queries that might return multiple result sets, or no result set.
        int status;
        do {
            MYSQL_RES* result = mysql_store_result(m_mysql_handle);
            if (result) {
                mysql_free_result(result);
            } else {  // No result set OR error
                if (mysql_field_count(m_mysql_handle) == 0) {
                    // No result set, which is OK for DML, SET, etc.
                    // Loop to check for more results if mysql_more_results is true.
                } else {
                    // Error occurred with mysql_store_result itself.
                    setErrorFromMySqlHandle(m_mysql_handle, context_message.empty() ? "Failed to retrieve result after query" : context_message + ": Failed to retrieve result");
                    return false;
                }
            }
            // Check if there are more results
            // mysql_next_result returns 0 if there are more results and prepares them,
            // >0 if an error occurred, -1 if there are no more results.
            status = mysql_next_result(m_mysql_handle);
            if (status > 0) {  // Error
                setErrorFromMySqlHandle(m_mysql_handle, context_message.empty() ? "Error processing multiple results" : context_message + ": Error processing results");
                return false;
            }
        } while (status == 0);  // Loop if status is 0 (more results processed)

        // After loop, status is -1 (no more results) or >0 (error, already handled).
        // mysql_errno() check after loop is redundant if status codes are handled correctly.
        return true;
    }

}  // namespace cpporm_mysql_transport