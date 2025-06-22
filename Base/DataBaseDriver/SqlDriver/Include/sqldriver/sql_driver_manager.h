// SqlDriver/Include/sqldriver/sql_driver_manager.h
#pragma once
#include <functional>
#include <map>  // For storing drivers and factories
#include <memory>
#include <mutex>  // For thread-safe access to static members
#include <string>
#include <vector>

// Forward declare SqlDatabase to avoid circular dependency if SqlDatabase needs SqlDriverManager
namespace cpporm_sqldriver {
    class SqlDatabase;
    class ISqlDriver;  // Already forward declared if i_sql_driver.h is included by SqlDatabase
}  // namespace cpporm_sqldriver

namespace cpporm_sqldriver {

    class SqlDriverManager {
      public:
        using DriverFactory = std::function<std::unique_ptr<ISqlDriver>()>;

        // Connection management
        // Returns a SqlDatabase object. The caller owns the unique_ptr to the driver inside SqlDatabase.
        static SqlDatabase addDatabase(const std::string& driverType, const std::string& connectionName = defaultConnectionName());

        // Retrieves an existing database connection.
        // Note: SqlDatabase objects themselves are not stored directly in the manager in this design.
        // This method would typically re-create a SqlDatabase wrapper around a potentially pooled or re-established driver.
        // For simplicity, if we are not managing SqlDatabase objects themselves, this might be better named
        // 'createDatabaseAccess' or similar. Or, it could return a reference/pointer if we store them (adds complexity).
        // Let's assume for now `database()` re-constructs a SqlDatabase object using a stored factory or a new driver instance if not connection pooling.
        // If `connectionName` already exists and implies a live, shared connection (pooling), then it's more complex.
        // For a simple manager, `addDatabase` creates, and `database` might just re-create or error if not found.
        // Let's refine: `database()` will attempt to find a previously "added" configuration if `open` is false,
        // or create and open if `open` is true. This implies some state per connectionName.
        // For now, `database()` will be similar to `addDatabase` but might check if a driver for `connectionName` already implies specific params.
        // This part needs careful design based on how connections are "managed" vs "created".

        // Let's simplify: `database()` returns a new SqlDatabase instance, potentially configured by `connectionName`.
        // If a connection with `connectionName` was previously created and its params are stored, `open=false` would return it configured but closed.
        // This still requires the manager to store more than just factories.
        //
        // Simpler model for now:
        // addDatabase is the primary way to get a SqlDatabase configured with a driver.
        // database() is a convenience that calls addDatabase and optionally opens.
        static SqlDatabase database(const std::string& connectionName = defaultConnectionName(), bool open = true);

        static void removeDatabase(const std::string& connectionName);  // What does this remove? The factory? A stored SqlDatabase instance?
                                                                        // If we only store factories, this might not make sense unless we are un-registering a connection configuration.
                                                                        // For now, assume it refers to removing a named configuration if we were to store them.
                                                                        // Or, if we stored SqlDatabase instances (not recommended for static manager), it would remove that.
                                                                        // Let's assume it removes any configuration associated with connectionName for now.

        static bool contains(const std::string& connectionName = defaultConnectionName());  // Checks if a configuration for connectionName exists.

        // Driver information
        static std::vector<std::string> drivers();  // Lists registered driver *types*
        static bool isDriverAvailable(const std::string& driverType);

        static std::string defaultConnectionName();

        // Driver registration (called by specific driver plugins/modules)
        static bool registerDriver(const std::string& driverName, DriverFactory factory);
        static void unregisterDriver(const std::string& driverName);  // Optional

      private:
        SqlDriverManager() = delete;  // Static class

        // Internal storage for factories and potentially named connection configurations
        // This requires a static PImpl or static members directly.
        struct ManagerData {
            std::map<std::string, DriverFactory> driverFactories;
            // If we want to manage "named connections" beyond just driver type:
            // std::map<std::string, ConnectionParameters> namedConnectionParams;
            // std::map<std::string, std::string> namedConnectionDriverTypes; // driverType for a connectionName
            std::string defaultConnName = "qt_sql_default_connection";
            std::mutex managerMutex;
        };

        static ManagerData& data();  // Access to static data
    };

}  // namespace cpporm_sqldriver