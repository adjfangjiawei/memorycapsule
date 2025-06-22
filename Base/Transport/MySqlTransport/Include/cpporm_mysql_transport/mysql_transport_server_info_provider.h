#pragma once

#include <mysql/mysql.h>

#include <string>

namespace cpporm_mysql_transport {

    // This class is very simple, could also be static methods or part of Connection directly.
    class MySqlTransportServerInfoProvider {
      public:
        // Constructor might not be needed if all methods are static or take MYSQL*
        MySqlTransportServerInfoProvider() = default;

        std::string getServerVersionString(MYSQL* mysql_handle) const;
        unsigned long getServerVersionNumber(MYSQL* mysql_handle) const;  // e.g., 80023 for 8.0.23
        std::string getHostInfo(MYSQL* mysql_handle, bool is_connected) const;
        // Potentially add more info getters, e.g., thread ID, protocol version
    };

}  // namespace cpporm_mysql_transport