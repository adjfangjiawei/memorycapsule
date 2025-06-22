#include "cpporm_mysql_transport/mysql_transport_charset_handler.h"

#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_connection.h"  // For setting errors on the connection context

namespace cpporm_mysql_transport {

    MySqlTransportCharsetHandler::MySqlTransportCharsetHandler(MySqlTransportConnection* connection_context) : m_conn_ctx(connection_context) {
        if (!m_conn_ctx) {
            // This is a programming error.
            // MySqlTransportConnection should ensure this is constructed with a valid context.
            // If we want to be super robust, this handler could have an 'isValid' state.
        }
    }

    bool MySqlTransportCharsetHandler::setClientCharset(MYSQL* mysql_handle, const std::string& charset_name, bool is_pre_connect) {
        if (!mysql_handle) {
            if (m_conn_ctx) {
                m_conn_ctx->setErrorManually(MySqlTransportError::Category::InternalError, "CharsetHandler: MySQL handle not initialized for setClientCharset operation.");
            }
            return false;
        }
        if (charset_name.empty()) {
            if (m_conn_ctx) {
                m_conn_ctx->setErrorManually(MySqlTransportError::Category::ApiUsageError, "CharsetHandler: Charset name cannot be empty for setClientCharset.");
            }
            return false;
        }

        // The MySqlTransportConnection is responsible for clearing its own error state (m_last_error)
        // before calling this component's method. This component will then use the connection's
        // setErrorFromMySqlHandle or setErrorManually to set the error if its specific operation fails.

        int err_code = 0;
        if (is_pre_connect) {
            // Called before mysql_real_connect, use mysql_options
            err_code = mysql_options(mysql_handle, MYSQL_SET_CHARSET_NAME, charset_name.c_str());
        } else {
            // Called after mysql_real_connect, use mysql_set_character_set
            if (m_conn_ctx && !m_conn_ctx->isConnected()) {  // Sanity check
                m_conn_ctx->setErrorManually(MySqlTransportError::Category::ConnectionError, "CharsetHandler: Attempted to set charset on a non-connected session (post-connect path).");
                return false;
            }
            err_code = mysql_set_character_set(mysql_handle, charset_name.c_str());
        }

        if (err_code != 0) {
            if (m_conn_ctx) {
                // Let the connection set its error state using its handle, providing context.
                m_conn_ctx->setErrorFromMySqlHandle(mysql_handle, "Failed to set client character set to '" + charset_name + "'");
            }
            return false;
        }

        // If successful, MySqlTransportConnection might update its cached m_current_params.charset
        if (m_conn_ctx && !is_pre_connect) {  // Only update if connected and successful
            // MySqlTransportConnection should manage its m_current_params directly after calling this.
        }
        return true;
    }

    std::optional<std::string> MySqlTransportCharsetHandler::getClientCharset(MYSQL* mysql_handle, bool is_connected) const {
        if (!mysql_handle) {
            // Should not be called by MySqlTransportConnection if handle is null.
            if (m_conn_ctx) {
                // This might be an internal logic error if conn_ctx allowed this call.
                // m_conn_ctx->setErrorManually(MySqlTransportError::Category::InternalError, "CharsetHandler: Null handle for getClientCharset.");
            }
            return std::nullopt;
        }

        if (is_connected) {
            const char* charset_c_str = mysql_character_set_name(mysql_handle);
            if (charset_c_str) {
                return std::string(charset_c_str);
            }
            // If mysql_character_set_name returns null, it might indicate an error or unknown state.
            // The MySqlTransportConnection could check its own error status after this call if nullopt is unexpected.
            if (m_conn_ctx && mysql_errno(mysql_handle) != 0) {
                // m_conn_ctx->setErrorFromMySqlHandle(mysql_handle, "Failed to get client character set name.");
                // Best not to modify error state in a const getter. Caller should handle.
            }
        } else {
            // If not connected, the actual charset on the server side isn't established.
            // MySqlTransportConnection might have a cached 'intended' charset from params,
            // but this component (CharsetHandler) only deals with the live MYSQL handle.
        }
        return std::nullopt;  // Indicates charset could not be determined from the live handle in its current state
    }

}  // namespace cpporm_mysql_transport