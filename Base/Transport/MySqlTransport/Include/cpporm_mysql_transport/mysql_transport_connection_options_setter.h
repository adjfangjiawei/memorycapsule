// cpporm_mysql_transport/mysql_transport_connection_options_setter.h
#pragma once

#include <mysql/mysql.h>

#include <map>
#include <string>

#include "cpporm_mysql_transport/mysql_transport_types.h"  // For MySqlTransportConnectionParams, MySqlTransportError

namespace cpporm_mysql_transport {

    class MySqlTransportConnection;  // Forward declaration for context

    class MySqlTransportConnectionOptionsSetter {
      public:
        explicit MySqlTransportConnectionOptionsSetter(MySqlTransportConnection* connection_context);

        // Applies options from params to the MYSQL handle BEFORE mysql_real_connect
        // Returns true on success, false on failure (error set in connection_context)
        bool applyPreConnectOptions(MYSQL* mysql_handle, const MySqlTransportConnectionParams& params);

      private:
        friend class MySqlTransportConnection;  // Added friend declaration
        MySqlTransportConnection* m_conn_ctx;   // Context to set errors

        // Helper to map SSL mode string to MySQL option value
        unsigned int mapSslModeStringToValue(const std::string& mode_str) const;
    };

}  // namespace cpporm_mysql_transport