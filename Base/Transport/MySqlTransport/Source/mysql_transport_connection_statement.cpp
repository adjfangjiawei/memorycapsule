// cpporm_mysql_transport/mysql_transport_connection_statement.cpp
#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"  // Definition of MySqlTransportStatement

namespace cpporm_mysql_transport {

    std::unique_ptr<MySqlTransportStatement> MySqlTransportConnection::createStatement(const std::string& query) {
        // Connection validity (m_mysql_handle != nullptr) is checked by MySqlTransportStatement constructor
        return std::make_unique<MySqlTransportStatement>(this, query);
    }

}  // namespace cpporm_mysql_transport