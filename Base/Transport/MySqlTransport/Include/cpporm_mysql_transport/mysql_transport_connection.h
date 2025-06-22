// cpporm_mysql_transport/mysql_transport_connection.h
#pragma once

#include <mysql/mysql.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

// Forward declare helper classes if they are only used by unique_ptr in private section
// Otherwise, include their headers directly.
// For PImpl, these are typical forward declarations:
// class MySqlTransportConnectionOptionsSetter;
// class MySqlTransportTransactionManager;
// class MySqlTransportCharsetHandler;
// class MySqlTransportServerInfoProvider;
// However, since they are included in the .cpp and their methods are called,
// it's better to include headers if they are not purely internal details of the PImpl.
// Given the current structure, they seem to be components.

#include "cpporm_mysql_transport/mysql_transport_charset_handler.h"
#include "cpporm_mysql_transport/mysql_transport_connection_options_setter.h"
#include "cpporm_mysql_transport/mysql_transport_server_info_provider.h"
#include "cpporm_mysql_transport/mysql_transport_transaction_manager.h"
#include "cpporm_mysql_transport/mysql_transport_types.h"

namespace cpporm_mysql_transport {

    class MySqlTransportStatement;  // Forward declaration
    void ensure_mysql_library_initialized();
    void try_mysql_library_end();

    class MySqlTransportConnection {
      public:
        MySqlTransportConnection();
        ~MySqlTransportConnection();

        MySqlTransportConnection(const MySqlTransportConnection&) = delete;
        MySqlTransportConnection& operator=(const MySqlTransportConnection&) = delete;
        MySqlTransportConnection(MySqlTransportConnection&& other) noexcept;
        MySqlTransportConnection& operator=(MySqlTransportConnection&& other) noexcept;

        bool connect(const MySqlTransportConnectionParams& params);
        void disconnect();
        bool isConnected() const;
        bool ping(std::optional<unsigned int> timeout_seconds = std::nullopt);

        std::unique_ptr<MySqlTransportStatement> createStatement(const std::string& query);

        // Transaction methods delegated to m_transaction_manager
        bool beginTransaction();
        bool commit();
        bool rollback();
        bool setTransactionIsolation(TransactionIsolationLevel level);
        std::optional<TransactionIsolationLevel> getTransactionIsolation() const;
        bool setSavepoint(const std::string& name);
        bool rollbackToSavepoint(const std::string& name);
        bool releaseSavepoint(const std::string& name);

        // Charset methods delegated to m_charset_handler
        bool setClientCharset(const std::string& charset_name);
        std::optional<std::string> getClientCharset() const;

        // Server info methods delegated to m_server_info_provider
        std::string getServerVersionString() const;
        unsigned long getServerVersionNumber() const;
        std::string getHostInfo() const;

        MySqlTransportError getLastError() const;
        std::string escapeString(const std::string& unescaped_str, bool treat_backslash_as_meta = true);

        MYSQL* getNativeHandle() const {
            return m_mysql_handle;
        }
        const MySqlTransportConnectionParams& getCurrentParams() const {
            return m_current_params;
        }

        // Methods for internal use by components (e.g., TransactionManager, CharsetHandler)
        // These should ideally be less public or managed via friend classes / interfaces.
        // For simplicity, making them public but with an underscore prefix to indicate intended internal use.
        bool _internalExecuteSimpleQuery(const std::string& query, const std::string& context_message);
        void setErrorFromMySqlHandle(MYSQL* handle_to_check_error_on, const std::string& context_message);
        void setErrorManually(MySqlTransportError::Category cat, const std::string& msg, int native_mysql_err = 0, const char* native_sql_state = nullptr, const char* native_mysql_msg = nullptr, unsigned int proto_errc = 0);
        void recordPreConnectOptionError(const std::string& option_error_message);

      private:
        void clearError();  // Clears m_last_error

        MYSQL* m_mysql_handle;
        bool m_is_connected;
        MySqlTransportConnectionParams m_current_params;
        MySqlTransportError m_last_error;
        TransactionIsolationLevel m_current_isolation_level;  // Cached isolation level for getTransactionIsolation

        // Components
        std::unique_ptr<MySqlTransportConnectionOptionsSetter> m_options_setter;
        std::unique_ptr<MySqlTransportTransactionManager> m_transaction_manager;
        std::unique_ptr<MySqlTransportCharsetHandler> m_charset_handler;
        std::unique_ptr<MySqlTransportServerInfoProvider> m_server_info_provider;
    };

}  // namespace cpporm_mysql_transport