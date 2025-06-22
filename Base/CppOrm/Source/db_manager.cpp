#include "cpporm/db_manager.h"  // 引用新的头文件

#include <QDebug>  // 仍然可以使用 Qt Core 的调试功能

// SqlDriverManager, SqlConnectionParameters, SqlDatabase, Error 已经在 db_manager.h 中包含

namespace cpporm {

    // openDatabase 现在返回 std::expected<std::string, Error>
    // 连接名现在是 std::string
    std::expected<std::string, Error> DbManager::openDatabase(const DbConfig &config) {
        std::string conn_name_std = config.connection_name;
        if (conn_name_std.empty()) {
            conn_name_std = DbConfig::generateUniqueConnectionName();
        }

        // 使用 cpporm_sqldriver::ConnectionParameters
        cpporm_sqldriver::ConnectionParameters driver_params = config.toDriverParameters();

        // SqlDriverManager::addDatabase 返回一个 SqlDatabase 对象
        // 我们需要检查它是否有效以及是否能打开
        // addDatabase 内部会处理驱动注册和创建
        // SqlDriverManager 本身不直接 "打开" 连接，而是 SqlDatabase 对象自己打开
        cpporm_sqldriver::SqlDatabase db = cpporm_sqldriver::SqlDriverManager::addDatabase(config.driver_type, conn_name_std);

        if (!db.isValid()) {
            // 获取更详细的错误信息，如果 SqlDatabase 提供了
            cpporm_sqldriver::SqlError last_err = db.lastError();
            return std::unexpected(Error(ErrorCode::DriverNotFound, "Failed to add database: Driver type '" + config.driver_type + "' might be unavailable or invalid. Connection name: " + conn_name_std + ". Driver Msg: " + last_err.text()));
        }

        // SqlDatabase::open(params) 负责实际的连接打开
        if (!db.open(driver_params)) {
            cpporm_sqldriver::SqlError last_err = db.lastError();
            cpporm_sqldriver::SqlDriverManager::removeDatabase(conn_name_std);  // 打开失败，移除它
            return std::unexpected(Error(ErrorCode::ConnectionFailed, "Failed to open database connection: " + last_err.text() + " (Native Code: " + last_err.nativeErrorCode() + ")", last_err.nativeErrorCodeNumeric()));
        }

        // 如果驱动支持并且配置中指定了字符集，则尝试设置
        // 注意: SqlDatabase::setClientCharset 应该在 open 之后调用
        if (!config.client_charset.empty()) {
            if (!db.setClientCharset(config.client_charset)) {
                cpporm_sqldriver::SqlError charset_err = db.lastError();
                // 根据需求决定这是否是致命错误
                qWarning() << "DbManager::openDatabase: Failed to set client charset '" << QString::fromStdString(config.client_charset) << "' for connection" << QString::fromStdString(conn_name_std) << ". Error:" << QString::fromStdString(charset_err.text())
                           << ". Continuing without this charset setting.";
            }
        }
        return conn_name_std;
    }

    // getDatabase 返回 cpporm_sqldriver::SqlDatabase
    // 连接名现在是 std::string
    cpporm_sqldriver::SqlDatabase DbManager::getDatabase(const std::string &connection_name_str) {
        // SqlDriverManager::database() 返回一个 SqlDatabase 对象。
        // 第二个参数 bool open 默认为 true，表示如果连接不存在或未打开，则尝试打开它。
        // 这可能需要传递完整的 ConnectionParameters，但 SqlDriverManager
        // 应该已经存储了它们（或者需要一个更复杂的 getDatabase 实现）。
        // 假设 SqlDriverManager::database(name, true) 会处理打开逻辑。
        // 如果仅获取句柄而不保证打开，则用 false。
        return cpporm_sqldriver::SqlDriverManager::database(connection_name_str, false /* do not auto-open here, openDatabase should handle it */);
    }

    // closeDatabase 连接名现在是 std::string
    void DbManager::closeDatabase(const std::string &connection_name_str) {
        // SqlDriverManager::removeDatabase 会处理关闭和移除
        cpporm_sqldriver::SqlDriverManager::removeDatabase(connection_name_str);
    }

    // isConnectionValid 连接名现在是 std::string
    bool DbManager::isConnectionValid(const std::string &connection_name_str) {
        if (!cpporm_sqldriver::SqlDriverManager::contains(connection_name_str)) {
            return false;
        }
        // 获取数据库句柄，但不尝试打开它（如果它尚未打开）
        cpporm_sqldriver::SqlDatabase db = cpporm_sqldriver::SqlDriverManager::database(connection_name_str, false);
        return db.isValid() && db.isOpen();
    }

}  // namespace cpporm