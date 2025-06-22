// SqlDriver/Source/sql_driver_manager.cpp
#include "sqldriver/sql_driver_manager.h"

#include <map>
#include <memory>  // For std::shared_ptr and std::unique_ptr (factory can still return unique_ptr)
#include <mutex>
#include <stdexcept>  // For std::runtime_error if needed

#include "sqldriver/i_sql_driver.h"
#include "sqldriver/sql_connection_parameters.h"  // For ConnectionParameters in database() if needed
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
                return SqlDatabase(driverType, connectionName, nullptr);  // Pass nullptr shared_ptr
            }
            factory_to_call = factory_it->second;  // Get the factory
        }  // Mutex released here

        std::unique_ptr<ISqlDriver> unique_driver_instance = nullptr;  // Factory returns unique_ptr
        if (factory_to_call) {
            try {
                unique_driver_instance = factory_to_call();  // Call factory outside the lock
            } catch (const std::exception& /*e*/) {
                // Factory call threw an exception. driverInstance remains nullptr.
                // Optionally log here: some_logging_system("Driver factory for " + driverType + " threw: " + e.what());
            } catch (...) {
                // Catch all for other potential issues from factory.
            }
        }

        // Convert unique_ptr to shared_ptr for SqlDatabase
        std::shared_ptr<ISqlDriver> shared_driver_instance = std::move(unique_driver_instance);

        if (!shared_driver_instance) {
            // Factory failed to create driver instance or factory_to_call was null after lock release.
            return SqlDatabase(driverType, connectionName, nullptr);  // Pass nullptr shared_ptr
        }

        return SqlDatabase(driverType, connectionName, std::move(shared_driver_instance));
    }

    SqlDatabase SqlDriverManager::database(const std::string& connectionName, bool open) {
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
                // For non-default connection names, if `connectionName` is intended to be a `driverType`.
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

        std::unique_ptr<ISqlDriver> unique_driver_instance = nullptr;
        if (factory_to_call) {
            try {
                unique_driver_instance = factory_to_call();
            } catch (...) { /* Factory failed */
            }
        } else {
            // This case means factory_to_call was null after lock release.
            return SqlDatabase(driverTypeToUse.empty() ? "UnknownDriver" : driverTypeToUse, connectionName, nullptr);
        }

        std::shared_ptr<ISqlDriver> shared_driver_instance = std::move(unique_driver_instance);
        SqlDatabase db(driverTypeToUse, connectionName, std::move(shared_driver_instance));

        if (open && db.isValid()) {
            // SqlDatabase::open() will use its internally stored m_parameters.
            // If these are empty, it depends on the driver's default behavior.
            db.open();
        }
        return db;
    }

    void SqlDriverManager::removeDatabase(const std::string& /*connectionName*/) {
        // In the current design where SqlDriverManager doesn't manage active SqlDatabase
        // instances or named configurations (beyond factories by driver type), this method
        // is largely conceptual. If it were to remove a configuration for a 'connectionName',
        // that would require storing such configurations.
        // For now, it's a no-op concerning active connections, as Session owns its SqlDatabase.
        std::lock_guard<std::mutex> lock(data().managerMutex);
        // If 'data().namedConnectionParams' or similar existed, one would erase from it here.
    }

    bool SqlDriverManager::contains(const std::string& connectionName) {
        // This checks if a driver factory exists for the given name (if treated as driverType)
        // or if it's the default name and any driver is registered.
        // It does NOT check if an active SqlDatabase instance with this name exists.
        std::lock_guard<std::mutex> lock(data().managerMutex);
        if (connectionName == defaultConnectionName()) {
            return !data().driverFactories.empty();
        }
        return data().driverFactories.count(connectionName) > 0;
    }

    // --- Driver Information ---
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
        std::lock_guard<std::mutex> lock(data().managerMutex);  // Ensure consistent access pattern to data()
        return data().defaultConnName;
    }

    // --- Driver Registration ---
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