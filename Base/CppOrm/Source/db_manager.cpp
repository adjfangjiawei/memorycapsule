// Base/CppOrm/Source/db_manager.cpp
#include "cpporm/db_manager.h"

#include <QDebug>  // For qWarning

#include "sqldriver/sql_database.h"        // For cpporm_sqldriver::SqlDatabase
#include "sqldriver/sql_driver_manager.h"  // For cpporm_sqldriver::SqlDriverManager

namespace cpporm {

    std::expected<cpporm_sqldriver::SqlDatabase, Error> DbManager::openDatabase(const DbConfig &config) {
        std::string assigned_conn_name = config.connection_name;
        if (assigned_conn_name.empty()) {
            assigned_conn_name = DbConfig::generateUniqueConnectionName();
        }

        // 1. Get a SqlDatabase shell with the correct driver from SqlDriverManager
        cpporm_sqldriver::SqlDatabase db = cpporm_sqldriver::SqlDriverManager::addDatabase(config.driver_type, assigned_conn_name);

        if (!db.isValid()) {                                       // Checks if driver was loaded successfully into SqlDatabase
            cpporm_sqldriver::SqlError last_err = db.lastError();  // Get error from SqlDatabase constructor if driver failed
            std::string error_msg = "Failed to initialize database driver: Type '" + config.driver_type + "'. Connection name: " + assigned_conn_name;
            if (last_err.isValid()) {
                error_msg += ". Driver Msg: " + last_err.text();
            }
            return std::unexpected(Error(ErrorCode::DriverNotFound, error_msg));
        }

        // 2. Set connection parameters on the SqlDatabase object
        // SqlDatabase::open will use these parameters.
        // Or, pass them directly to open()
        cpporm_sqldriver::ConnectionParameters driver_params = config.toDriverParameters();
        // We can set these on db.m_parameters, or pass to db.open().
        // SqlDatabase::open(const ConnectionParameters&) is preferred.

        // 3. Open the connection
        if (!db.open(driver_params)) {
            cpporm_sqldriver::SqlError last_err = db.lastError();
            return std::unexpected(Error(ErrorCode::ConnectionFailed, "Failed to open database connection '" + assigned_conn_name + "': " + last_err.text() + " (Native Code: " + last_err.nativeErrorCode() + ")", last_err.nativeErrorCodeNumeric()));
        }

        // 4. Set client charset if specified
        if (!config.client_charset.empty()) {
            if (!db.setClientCharset(config.client_charset)) {
                cpporm_sqldriver::SqlError charset_err = db.lastError();
                qWarning() << "DbManager::openDatabase: Failed to set client charset '" << QString::fromStdString(config.client_charset) << "' for connection" << QString::fromStdString(assigned_conn_name) << ". Error:" << QString::fromStdString(charset_err.text())
                           << ". Continuing without this charset setting.";
                // This is not treated as a fatal error for opening the database.
            }
        }
        return std::move(db);  // Return the opened and configured SqlDatabase object
    }

    // Commented out methods as Session will own the SqlDatabase handle directly.
    // If these are needed, their implementation must change as SqlDriverManager does not store active instances.
    /*
    cpporm_sqldriver::SqlDatabase DbManager::getDatabase(const std::string &connection_name_str) {
        // This would return a NEW SqlDatabase, not an existing one from a pool.
        return cpporm_sqldriver::SqlDriverManager::database(connection_name_str, false);
    }

    void DbManager::closeDatabase(const std::string &connection_name_str) {
        // SqlDatabase objects should be closed by their owners (e.g. Session destructor).
        // SqlDriverManager::removeDatabase is conceptual in current design.
        // cpporm_sqldriver::SqlDriverManager::removeDatabase(connection_name_str);
    }

    bool DbManager::isConnectionValid(const std::string &connection_name_str) {
        // To check validity, one would need the actual SqlDatabase instance.
        // This function cannot reliably check a connection by name if the manager doesn't store instances.
        cpporm_sqldriver::SqlDatabase db = cpporm_sqldriver::SqlDriverManager::database(connection_name_str, false);
        return db.isValid() && db.isOpen(); // This checks a NEWLY created (or potentially default) one.
    }
    */

}  // namespace cpporm