// cpporm/db_manager.h
#ifndef cpporm_DB_MANAGER_H
#define cpporm_DB_MANAGER_H

#include <expected>
#include <memory>
#include <string>

#include "cpporm/error.h"
#include "sqldriver/sql_connection_parameters.h"
#include "sqldriver/sql_database.h"  // Now returns SqlDatabase directly
#include "sqldriver/sql_driver_manager.h"

namespace cpporm {

    struct DbConfig {
        std::string driver_type;
        std::string host_name = "127.0.0.1";
        int port = -1;
        std::string database_name;
        std::string user_name;
        std::string password;
        std::string connect_options;
        std::string client_charset;
        std::string connection_name;  // Optional: if SqlDatabase is to be named for later retrieval (not used by Session directly if Session owns the handle)

        static std::string generateUniqueConnectionName() {
            static long long counter = 0;
            // Using a more descriptive prefix for SqlDatabase connection names if they are still used by DriverManager for anything
            return "cpporm_sqldb_conn_" + std::to_string(++counter);
        }

        cpporm_sqldriver::ConnectionParameters toDriverParameters() const {
            cpporm_sqldriver::ConnectionParameters params;
            // driver_type is used by SqlDriverManager to get the factory, not a parameter for SqlDatabase::open
            params.setHostName(host_name);
            if (port > 0) {
                params.setPort(port);
            }
            params.setDbName(database_name);
            params.setUserName(user_name);
            params.setPassword(password);
            if (!connect_options.empty()) {
                params.setConnectOptions(connect_options);
            }
            // client_charset is handled separately after open if needed
            return params;
        }
    };

    class DbManager {
      public:
        DbManager() = delete;

        // Changed return type: directly returns the SqlDatabase object (or an error)
        static std::expected<cpporm_sqldriver::SqlDatabase, Error> openDatabase(const DbConfig &config);

        // The following methods become problematic if SqlDriverManager doesn't manage active SqlDatabase instances by name.
        // Session will now own its SqlDatabase handle.
        // These might need to be removed or re-thought if global access to specific connections by name is truly needed.
        // For now, I'll comment them out as Session will get its handle from openDatabase.
        /*
        static cpporm_sqldriver::SqlDatabase getDatabase(const std::string &connection_name_str = cpporm_sqldriver::SqlDriverManager::defaultConnectionName());
        static void closeDatabase(const std::string &connection_name_str);
        static bool isConnectionValid(const std::string &connection_name_str);
        */
    };

}  // namespace cpporm

#endif  // cpporm_DB_MANAGER_H