#ifndef cpporm_DB_MANAGER_H
#define cpporm_DB_MANAGER_H

#include <expected>
#include <memory>
#include <string>  // 使用 std::string

#include "cpporm/error.h"
#include "sqldriver/sql_connection_parameters.h"  // 新的连接参数
#include "sqldriver/sql_database.h"               // 新的数据库对象
#include "sqldriver/sql_driver_manager.h"         // 新的驱动管理器

namespace cpporm {

    struct DbConfig {
        std::string driver_type;  // 例如 "MYSQL", "PSQL", "SQLITE"
        std::string host_name = "127.0.0.1";
        int port = -1;
        std::string database_name;
        std::string user_name;
        std::string password;
        std::string connect_options;
        std::string client_charset;   // 例如 "utf8mb4"
        std::string connection_name;  // 连接名，如果为空则自动生成

        static std::string generateUniqueConnectionName() {
            static long long counter = 0;
            return "cpporm_sqldrv_conn_" + std::to_string(++counter);
        }

        cpporm_sqldriver::ConnectionParameters toDriverParameters() const {
            cpporm_sqldriver::ConnectionParameters params;
            params.setDriverType(driver_type);  // 驱动类型现在由 SqlDriverManager 处理
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
            // client_charset 在 SqlDatabase 打开后单独设置
            return params;
        }
    };

    class DbManager {
      public:
        DbManager() = delete;

        // openDatabase 返回 std::expected<std::string, Error>
        // connection_name 现在是 std::string
        static std::expected<std::string, Error> openDatabase(const DbConfig &config);

        // getDatabase 返回 cpporm_sqldriver::SqlDatabase
        // connection_name_str 现在是 std::string
        static cpporm_sqldriver::SqlDatabase getDatabase(const std::string &connection_name_str = cpporm_sqldriver::SqlDriverManager::defaultConnectionName());

        // closeDatabase connection_name_str 现在是 std::string
        static void closeDatabase(const std::string &connection_name_str);

        // isConnectionValid connection_name_str 现在是 std::string
        static bool isConnectionValid(const std::string &connection_name_str);
    };

}  // namespace cpporm

#endif  // cpporm_DB_MANAGER_H