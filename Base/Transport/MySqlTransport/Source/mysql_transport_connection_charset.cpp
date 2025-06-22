// cpporm_mysql_transport/mysql_transport_connection_charset.cpp
#include "cpporm_mysql_transport/mysql_transport_charset_handler.h"
#include "cpporm_mysql_transport/mysql_transport_connection.h"

namespace cpporm_mysql_transport {

    bool MySqlTransportConnection::setClientCharset(const std::string& charset_name) {
        if (!m_charset_handler) {
            setErrorManually(MySqlTransportError::Category::InternalError, "Charset handler not initialized.");
            return false;
        }
        // is_pre_connect is true if not m_is_connected
        bool success = m_charset_handler->setClientCharset(m_mysql_handle, charset_name, !m_is_connected);
        if (success) {
            m_current_params.charset = charset_name;  // Update cached intended charset
        }
        return success;
    }

    std::optional<std::string> MySqlTransportConnection::getClientCharset() const {
        if (!m_charset_handler) {
            return std::nullopt;
        }

        if (m_is_connected && m_mysql_handle) {
            auto live_charset = m_charset_handler->getClientCharset(m_mysql_handle, m_is_connected);
            if (live_charset) {
                // If live_charset is different from m_current_params.charset,
                // it means it was changed externally or by server default.
                // A non-const version could update m_current_params.charset here.
                return live_charset;
            }
        }

        // Fallback to configured/intended charset if not connected or live query failed
        if (m_current_params.charset.has_value() && !m_current_params.charset.value().empty()) {
            return m_current_params.charset.value();
        }
        return std::nullopt;
    }

}  // namespace cpporm_mysql_transport