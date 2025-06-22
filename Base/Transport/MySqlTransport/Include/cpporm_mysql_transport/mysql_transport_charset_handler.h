// cpporm_mysql_transport/mysql_transport_charset_handler.h
#pragma once

#include <mysql/mysql.h>

#include <optional>
#include <string>

#include "cpporm_mysql_transport/mysql_transport_types.h"  // For MySqlTransportError

namespace cpporm_mysql_transport {

    class MySqlTransportConnection;  // Forward declaration

    class MySqlTransportCharsetHandler {
      public:
        explicit MySqlTransportCharsetHandler(MySqlTransportConnection* connection_context);

        // Sets charset using mysql_set_character_set() if connected,
        // or mysql_options(MYSQL_SET_CHARSET_NAME) if before connection.
        // The 'is_pre_connect' flag indicates if this is called before mysql_real_connect.
        bool setClientCharset(MYSQL* mysql_handle, const std::string& charset_name, bool is_pre_connect);

        std::optional<std::string> getClientCharset(MYSQL* mysql_handle, bool is_connected) const;

      private:
        friend class MySqlTransportConnection;  // Added friend declaration
        MySqlTransportConnection* m_conn_ctx;
    };

}  // namespace cpporm_mysql_transport