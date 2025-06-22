// SqlDriver/Source/sql_driver_manager.cpp
#include "sqldriver/sql_driver_manager.h"

#include <map>
#include <memory>  // For std::unique_ptr
#include <mutex>
#include <stdexcept>  // For std::runtime_error

#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_connection_parameters.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"

namespace cpporm_sqldriver {

    // --- Static Data Accessor ---
    SqlDriverManager::ManagerData& SqlDriverManager::data() {
        static ManagerData manager_data;  // C++11 guarantees thread-safe initialization
        return manager_data;
    }

    // --- Connection Management ---
    SqlDatabase SqlDriverManager::addDatabase(const std::string& driverType, const std::string& connectionName) {
        DriverFactory factory_to_call = nullptr;
        {  // Scope for lock
            std::lock_guard<std::mutex> lock(data().managerMutex);
            auto& factories = data().driverFactories;
            auto factory_it = factories.find(driverType);

            if (factory_it == factories.end()) {
                // Driver type not registered, return SqlDatabase that will be invalid
                // SqlDatabase constructor will set an error if driverInstance is null
                return SqlDatabase(driverType, connectionName, nullptr);
            }
            factory_to_call = factory_it->second;  // Get the factory
        }  // Mutex released here

        std::unique_ptr<ISqlDriver> driverInstance = nullptr;
        if (factory_to_call) {
            try {
                driverInstance = factory_to_call();  // Call factory outside the lock
            } catch (const std::exception& e) {
                // Factory call threw an exception. driverInstance remains nullptr.
                // The SqlDatabase constructor will handle the null driverInstance.
                // Optionally log here, but SqlDatabase constructor will also "know".
                // For example: some_logging_system("Driver factory for " + driverType + " threw: " + e.what());
            } catch (...) {
                // Catch all for other potential issues from factory.
            }
        }

        if (!driverInstance) {
            // Factory failed to create driver instance or factory_to_call was null after lock release.
            return SqlDatabase(driverType, connectionName, nullptr);
        }

        return SqlDatabase(driverType, connectionName, std::move(driverInstance));
    }

    SqlDatabase SqlDriverManager::database(const std::string& connectionName, bool open) {
        // This method's purpose is primarily to support Qt's QSqlDatabase::database() model.
        // In our design, DbManager::openDatabase is the preferred way to get a configured and opened SqlDatabase.
        // If called with a non-default name, it implies a pre-configured, named connection,
        // which SqlDriverManager doesn't directly manage beyond driver factories.
        // For the default connection, it might try to use the first registered driver.

        std::string driverTypeToUse;
        DriverFactory factory_to_call = nullptr;

        {  // Scope for lock
            std::lock_guard<std::mutex> lock(data().managerMutex);
            auto& factories = data().driverFactories;

            if (connectionName == defaultConnectionName()) {
                if (!factories.empty()) {
                    driverTypeToUse = factories.begin()->first;
                    factory_to_call = factories.begin()->second;
                } else {
                    // No drivers registered, cannot create default connection
                    return SqlDatabase("NoDriversRegistered", connectionName, nullptr);
                }
            } else {
                // For non-default connection names, this manager doesn't store specific
                // configurations or active instances. It can only create a new SqlDatabase
                // if `connectionName` is treated as `driverType`.
                // This is likely a misuse if `connectionName` is dynamic like "cpporm_sqldrv_conn_1".
                // However, if a user explicitly calls SqlDriverManager::database("MYSQL", true),
                // it should work like addDatabase("MYSQL", "MYSQL").
                // For robustness, let's assume `connectionName` here could be a `driverType`.
                auto factory_it = factories.find(connectionName);
                if (factory_it != factories.end()) {
                    driverTypeToUse = factory_it->first;
                    factory_to_call = factory_it->second;
                } else {
                    // Cannot determine driver type for this connectionName.
                    return SqlDatabase("UnknownDriverForConnectionName", connectionName, nullptr);
                }
            }
        }  // Mutex released

        std::unique_ptr<ISqlDriver> driverInstance = nullptr;
        if (factory_to_call) {
            try {
                driverInstance = factory_to_call();
            } catch (...) { /* Factory failed */
            }
        } else {
            // This case should ideally not be hit if factory_it was valid.
            // Could happen if defaultConnectionName path had no factories.
            return SqlDatabase(driverTypeToUse.empty() ? "UnknownDriver" : driverTypeToUse, connectionName, nullptr);
        }

        SqlDatabase db(driverTypeToUse, connectionName, std::move(driverInstance));

        if (open && db.isValid()) {
            // To open, SqlDatabase::open() needs ConnectionParameters.
            // The `db` object has its own m_parameters, which are initially empty.
            // This `open()` call will likely use default parameters or fail if the driver requires specific ones.
            // Unlike DbManager, SqlDriverManager doesn't have the `DbConfig` to populate detailed params.
            // This means the `open=true` here is less useful unless default driver params are sufficient.
            db.open();  // Attempt to open with internal (likely default/empty) parameters
                        // The success/failure and error are handled within db.open() and accessible via db.lastError().
        }
        return db;
    }

    void SqlDriverManager::removeDatabase(const std::string& /*connectionName*/) {
        std::lock_guard<std::mutex> lock(data().managerMutex);
        // In the current design, SqlDriverManager does not manage SqlDatabase instances
        // or named configurations beyond factories. So, this is largely a conceptual no-op
        // unless we expand its role to store/manage configurations per connectionName.
    }

    bool SqlDriverManager::contains(const std::string& connectionName) {
        std::lock_guard<std::mutex> lock(data().managerMutex);
        // If we only manage driver factories by type, checking if a 'connectionName'
        // (which could be dynamic) "contains" is tricky.
        // It could mean: "is there a configuration for this name?" (not implemented)
        // or "is the default connection name configured?"
        // or "is connectionName a registered driver type?".
        // For now, if it's the default connection name, it "contains" if any driver is registered.
        // If it's another name, it "contains" if that name is a registered driver type.
        if (connectionName == defaultConnectionName()) {
            return !data().driverFactories.empty();
        }
        return data().driverFactories.count(connectionName) > 0;
    }

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
        // No lock needed for reading a const static member after initialization
        // or if data().defaultConnName is itself thread-safe / constant after init.
        // However, to be consistent with `data()` access:
        std::lock_guard<std::mutex> lock(data().managerMutex);
        return data().defaultConnName;
    }

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