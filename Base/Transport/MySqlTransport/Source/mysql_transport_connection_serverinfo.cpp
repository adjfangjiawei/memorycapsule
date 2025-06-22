// cpporm_mysql_transport/mysql_transport_connection_serverinfo.cpp
#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_server_info_provider.h"

namespace cpporm_mysql_transport {

    std::string MySqlTransportConnection::getServerVersionString() const {
        if (!m_server_info_provider || !m_mysql_handle) {
            // Cannot set error in const method
            return "Not available";
        }
        return m_server_info_provider->getServerVersionString(m_mysql_handle);
    }

    unsigned long MySqlTransportConnection::getServerVersionNumber() const {
        if (!m_server_info_provider || !m_mysql_handle) {
            return 0;
        }
        return m_server_info_provider->getServerVersionNumber(m_mysql_handle);
    }

    std::string MySqlTransportConnection::getHostInfo() const {
        if (!m_server_info_provider || !m_mysql_handle) {
            return "Not available";
        }
        return m_server_info_provider->getHostInfo(m_mysql_handle, m_is_connected);
    }

}  // namespace cpporm_mysql_transport