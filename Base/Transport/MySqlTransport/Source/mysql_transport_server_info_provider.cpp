#include "cpporm_mysql_transport/mysql_transport_server_info_provider.h"

#include <mysql/mysql.h>  // Ensure MYSQL type is fully defined

namespace cpporm_mysql_transport {

    // No constructor needed if methods are simple wrappers or connection context isn't stored.

    std::string MySqlTransportServerInfoProvider::getServerVersionString(MYSQL* mysql_handle) const {
        if (mysql_handle) {
            const char* server_info = mysql_get_server_info(mysql_handle);
            if (server_info) {
                return std::string(server_info);
            }
        }
        // Return empty or a specific "N/A" string if handle is null or info is null.
        // Throwing an exception might be too aggressive for a simple info getter.
        return "";
    }

    unsigned long MySqlTransportServerInfoProvider::getServerVersionNumber(MYSQL* mysql_handle) const {
        if (mysql_handle) {
            return mysql_get_server_version(mysql_handle);
        }
        return 0;  // 0 can indicate an error or that the handle is invalid.
    }

    std::string MySqlTransportServerInfoProvider::getHostInfo(MYSQL* mysql_handle, bool is_connected) const {
        // mysql_get_host_info() typically requires an active connection to return meaningful data.
        if (mysql_handle && is_connected) {
            const char* host_info = mysql_get_host_info(mysql_handle);
            if (host_info) {
                return std::string(host_info);
            }
        }
        return "";
    }

    // Potential additions:
    // unsigned long MySqlTransportServerInfoProvider::getThreadId(MYSQL* mysql_handle, bool is_connected) const {
    //     if (mysql_handle && is_connected) {
    //         return mysql_thread_id(mysql_handle);
    //     }
    //     return 0; // Or some error indicator
    // }
    //
    // unsigned int MySqlTransportServerInfoProvider::getProtocolVersion(MYSQL* mysql_handle) const {
    //     if (mysql_handle) {
    //         return mysql_get_proto_info(mysql_handle);
    //     }
    //     return 0;
    // }

}  // namespace cpporm_mysql_transport