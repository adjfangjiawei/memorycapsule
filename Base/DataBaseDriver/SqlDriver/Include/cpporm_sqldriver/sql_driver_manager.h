// cpporm_sqldriver/sql_driver_manager.h
#pragma once
#include <functional>  // For std::function
#include <memory>
#include <string>
#include <vector>

#include "sql_database.h"

namespace cpporm_sqldriver {

    class ISqlDriver;  // 前向声明

    class SqlDriverManager {
      public:
        using DriverFactory = std::function<std::unique_ptr<ISqlDriver>()>;

        // 连接管理
        static SqlDatabase addDatabase(const std::string& driverType, const std::string& connectionName = defaultConnectionName());
        static SqlDatabase database(const std::string& connectionName = defaultConnectionName(), bool open = true);
        static void removeDatabase(const std::string& connectionName);
        static bool contains(const std::string& connectionName = defaultConnectionName());

        // 驱动信息
        static std::vector<std::string> drivers();                     // 列出已注册的驱动类型
        static bool isDriverAvailable(const std::string& driverType);  // 检查驱动是否可用

        static std::string defaultConnectionName();

        // 驱动注册 (由具体驱动实现模块在初始化时调用)
        static bool registerDriver(const std::string& driverName, DriverFactory factory);
        // static void unregisterDriver(const std::string& driverName); // 可选

      private:
        SqlDriverManager() = delete;  // 静态类

        class Private;  // PImpl for static data
        static Private* d();
    };

}  // namespace cpporm_sqldriver