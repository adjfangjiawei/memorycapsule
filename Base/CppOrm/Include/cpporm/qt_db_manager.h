#ifndef cpporm_QT_DB_MANAGER_H
#define cpporm_QT_DB_MANAGER_H

#include "cpporm/error.h" // 我们自定义的错误处理机制
#include <expected>        // For std::expected (C++23)
#include <memory> // For std::shared_ptr if managing QSqlDatabase instances
#include <mutex>  // For thread-safe initialization if needed
#include <string>

// QtSql 相关头文件
#include <QSqlDatabase> // Qt 数据库连接核心类
#include <QString>      // Qt 字符串类

namespace cpporm {

// 数据库配置结构体，适配 QtSql QSqlDatabase 的需求
struct QtDBConfig {
  std::string driver_name =
      "QMYSQL"; // Qt SQL 驱动名称 (例如 "QMYSQL", "QPSQL", "QSQLITE")
  std::string host_name = "127.0.0.1"; // 数据库主机地址
  int port = 3306;                     // 数据库端口 (-1 表示使用默认)
  std::string database_name = "test";  // 数据库名称
  std::string user_name = "root";      // 用户名
  std::string password = "";           // 密码
  std::string
      connect_options; // 连接选项 (例如
                       // "MYSQL_OPT_CONNECT_TIMEOUT=5;UNIX_SOCKET=/var/run/mysqld/mysqld.sock")
                       // 特别注意字符集，例如: "MYSQL_SET_CHARSET_NAME=utf8mb4"
                       // Qt 的字符集处理有时需要通过 connect_options。
  std::string
      connection_name; // QtSql
                       // 连接名，用于区分多个连接。如果为空，则使用默认连接。
                       // 建议总是指定一个唯一的名称。

  // 辅助函数，生成一个唯一的连接名（如果用户未提供）
  static std::string generateUniqueConnectionName() {
    static long long counter = 0;
    return "cpporm_conn_" + std::to_string(++counter);
  }
};

// QtDbManager 类负责管理 QSqlDatabase 连接
// 由于 QSqlDatabase::addDatabase 返回的 QSqlDatabase
// 对象是值类型且基于连接名管理，
// 这个类更多的是提供配置和获取已配置连接的便利方法。
class QtDbManager {
public:
  // 禁止实例化，作为纯静态工具类
  QtDbManager() = delete;

  // 初始化并打开一个数据库连接
  // config: 数据库配置
  // returns: 成功时返回配置中使用的连接名
  // (如果config.connection_name为空，则会生成一个), 失败时返回 cpporm::Error
  static std::expected<QString, Error> openDatabase(const QtDBConfig &config);

  // 获取一个已打开的数据库连接
  // connection_name: 之前 openDatabase 时使用的连接名
  // returns: QSqlDatabase 对象。如果连接不存在或无效，返回的
  // QSqlDatabase::isValid() 会是 false。 注意：返回的是 QSqlDatabase
  // 的一个副本，但它们都指向同一个底层连接。
  static QSqlDatabase
  getDatabase(const QString &connection_name = QSqlDatabase::defaultConnection);

  // 关闭并移除一个数据库连接
  // connection_name: 要关闭的连接名
  static void closeDatabase(const QString &connection_name);

  // 检查特定名称的连接是否存在且有效
  static bool isConnectionValid(const QString &connection_name);

private:
  // 用于确保 addDatabase 等操作的线程安全（如果从多线程调用）
  // static std::mutex db_mutex_;
};

} // namespace cpporm

#endif // cpporm_QT_DB_MANAGER_H