// SqlDriver/Source/sql_driver_manager.cpp
#include "sqldriver/sql_driver_manager.h"

#include <map>
#include <mutex>
#include <stdexcept>  // For std::runtime_error

#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_connection_parameters.h"  // For ConnectionParameters in database() if needed
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"

namespace cpporm_sqldriver {

    // --- Static Data Accessor ---
    SqlDriverManager::ManagerData& SqlDriverManager::data() {
        static ManagerData manager_data;
        return manager_data;
    }

    // --- Connection Management ---
    SqlDatabase SqlDriverManager::addDatabase(const std::string& driverType, const std::string& connectionName) {
        std::lock_guard<std::mutex> lock(data().managerMutex);

        auto& factories = data().driverFactories;
        auto factory_it = factories.find(driverType);

        if (factory_it == factories.end()) {
            // 驱动类型未注册，返回一个带有空驱动的 SqlDatabase
            // SqlDatabase 构造函数会处理空驱动的情况
            return SqlDatabase(driverType, connectionName, nullptr);
        }

        std::unique_ptr<ISqlDriver> driverInstance = factory_it->second();  // 调用工厂函数
        if (!driverInstance) {
            // 工厂未能创建驱动实例
            return SqlDatabase(driverType, connectionName, nullptr);
        }

        // 创建并返回 SqlDatabase 对象，SqlDatabase 对象现在拥有 driverInstance
        return SqlDatabase(driverType, connectionName, std::move(driverInstance));
    }

    SqlDatabase SqlDriverManager::database(const std::string& connectionName, bool open) {
        // 这个简化版本主要依赖于 addDatabase 的逻辑。
        // 它需要一个 driverType 来创建数据库。
        // 如果没有提供 driverType，并且没有预先配置的与 connectionName 关联的 driverType，
        // 则此函数无法确定要使用哪个驱动。

        // 假设：如果 connectionName 是默认连接名，并且有注册的驱动，则使用第一个注册的驱动。
        // 否则，用户应该使用 addDatabase 指定驱动类型。
        // 这是一个简化的处理，实际场景可能需要更复杂的配置管理。

        std::string driverToUse;
        std::unique_ptr<ISqlDriver> driverInstance = nullptr;

        {
            std::lock_guard<std::mutex> lock(data().managerMutex);
            if (connectionName == defaultConnectionName() && !data().driverFactories.empty()) {
                driverToUse = data().driverFactories.begin()->first;  // 使用第一个注册的驱动作为默认
                driverInstance = data().driverFactories.begin()->second();
            } else {
                // 如果 connectionName 不是默认的，或者没有驱动注册，
                // 我们无法安全地选择驱动。返回一个无效的 SqlDatabase。
                // 或者，如果有一个机制通过 connectionName 查找 driverType，则在这里使用。
                // 目前，我们返回一个没有驱动的 SqlDatabase，isValid() 会是 false。
                return SqlDatabase("UnknownDriver", connectionName, nullptr);
            }
        }
        // 确保在创建 SqlDatabase 之前 driverInstance 被正确创建
        if (!driverInstance && !driverToUse.empty()) {  // 如果上面逻辑取了 driverToUse 但 factory 没执行
            std::lock_guard<std::mutex> lock(data().managerMutex);
            auto factory_it = data().driverFactories.find(driverToUse);
            if (factory_it != data().driverFactories.end()) {
                driverInstance = factory_it->second();
            }
        }

        SqlDatabase db = SqlDatabase(driverToUse, connectionName, std::move(driverInstance));

        if (open && db.isValid()) {
            // 要打开连接，我们需要 ConnectionParameters。
            // 在这个简化模型中，我们没有存储与 connectionName 关联的参数。
            // 因此，尝试使用 db 对象内部可能已有的 m_parameters 打开，
            // 或者如果参数为空，则 open() 可能会失败或使用驱动的默认值。
            // 更好的做法是要求用户在调用 database() 后，显式设置参数并调用 open()。
            // 或者 database() 应该能够从某处检索这些参数。
            // 为保持与之前行为的某种一致性（虽然有缺陷），这里尝试用db内部的参数打开：
            if (!db.connectionParameters().empty()) {  // SqlDatabase::connectionParameters() 是 public const
                db.open(db.connectionParameters());
            } else {
                // 如果没有参数，open() 行为取决于 SqlDatabase::open() 的具体实现
                // 它可能会尝试用空参数打开，这通常会导致驱动使用默认值或失败
                db.open();
            }
            // 注意：这里的 open 调用的错误状态会由 db.lastError() 反映。
        }
        return db;
    }

    void SqlDriverManager::removeDatabase(const std::string& /*connectionName*/) {
        std::lock_guard<std::mutex> lock(data().managerMutex);
        // 当前设计中，SqlDriverManager 不管理 SqlDatabase 实例的生命周期，
        // 因此 removeDatabase 主要是概念性的，或者用于移除已命名的配置（如果将来实现）。
        // 目前无操作。
    }

    bool SqlDriverManager::contains(const std::string& /*connectionName*/) {
        std::lock_guard<std::mutex> lock(data().managerMutex);
        // 类似于 removeDatabase, 如果不存储实例或配置，此函数意义不大。
        // 它可以用来检查是否有与 connectionName 匹配的预配置（如果实现）。
        // 或者，如果 connectionName 暗示了驱动类型，可以检查该驱动类型是否已注册。
        return false;  // 占位符
    }

    // --- 驱动信息 ---
    std::vector<std::string> SqlDriverManager::drivers() {
        std::lock_guard<std::mutex> lock(data().managerMutex);
        std::vector<std::string> driver_names;
        driver_names.reserve(data().driverFactories.size());
        for (const auto& pair : data().driverFactories) {
            driver_names.push_back(pair.first);
        }
        return driver_names;
    }

    bool SqlDriverManager::isDriverAvailable(const std::string& driverType) {
        std::lock_guard<std::mutex> lock(data().managerMutex);
        return data().driverFactories.count(driverType) > 0;
    }

    std::string SqlDriverManager::defaultConnectionName() {
        std::lock_guard<std::mutex> lock(data().managerMutex);
        return data().defaultConnName;
    }

    // --- 驱动注册 ---
    bool SqlDriverManager::registerDriver(const std::string& driverName, DriverFactory factory) {
        if (driverName.empty() || !factory) {
            return false;
        }
        std::lock_guard<std::mutex> lock(data().managerMutex);
        data().driverFactories[driverName] = std::move(factory);
        return true;
    }

    void SqlDriverManager::unregisterDriver(const std::string& driverName) {
        std::lock_guard<std::mutex> lock(data().managerMutex);
        data().driverFactories.erase(driverName);
    }

}  // namespace cpporm_sqldriver