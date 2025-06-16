#include "cpporm/qt_db_manager.h"
#include <QDebug>    // For Qt-style debug output (optional)
#include <QSqlError> // For QSqlError related information
#include <QSqlQuery> // Needed for executing SET NAMES
#include <QUuid>     // For generating unique names if needed

// std::mutex cpporm::QtDbManager::db_mutex_; // Definition if mutex is used

namespace cpporm {

std::expected<QString, Error>
QtDbManager::openDatabase(const QtDBConfig &config) {
  // std::lock_guard<std::mutex> lock(db_mutex_); // Lock if multi-threaded
  // access to addDatabase

  QString conn_name = QString::fromStdString(config.connection_name);
  if (conn_name.isEmpty()) {
    conn_name =
        QString::fromStdString(QtDBConfig::generateUniqueConnectionName());
  }

  if (QSqlDatabase::contains(conn_name)) {
    QSqlDatabase existing_db = QSqlDatabase::database(conn_name, false);
    if (existing_db.isValid() && existing_db.isOpen()) {
      return conn_name;
    }
    QSqlDatabase::removeDatabase(conn_name);
  }

  QSqlDatabase db = QSqlDatabase::addDatabase(
      QString::fromStdString(config.driver_name), conn_name);

  if (!db.isValid()) {
    return std::unexpected(
        Error(ErrorCode::DriverNotFound,
              "Qt SQL Driver not valid or not found: " + config.driver_name +
                  ". Ensure the driver plugin (e.g., qsqlmysql.dll/so) is "
                  "available. Connection name: " +
                  conn_name.toStdString()));
  }

  db.setHostName(QString::fromStdString(config.host_name));
  if (config.port > 0) {
    db.setPort(config.port);
  }
  db.setDatabaseName(QString::fromStdString(config.database_name));
  db.setUserName(QString::fromStdString(config.user_name));
  db.setPassword(QString::fromStdString(config.password));
  if (!config.connect_options.empty()) {
    db.setConnectOptions(QString::fromStdString(config.connect_options));
  }

  if (!db.open()) {
    QSqlError q_error = db.lastError();
    return std::unexpected(
        Error(ErrorCode::ConnectionFailed,
              "Failed to open Qt database connection: " +
                  q_error.text().toStdString() +
                  " (Driver Error: " + q_error.driverText().toStdString() +
                  ", DB Error: " + q_error.databaseText().toStdString() + ")",
              q_error.nativeErrorCode().toInt(), ""));
  }

  // 在连接成功后，为 MySQL/MariaDB 设置字符集
  QString driverNameUpper =
      QString::fromStdString(config.driver_name).toUpper();
  if (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
    QSqlQuery set_names_query(db); // 使用当前连接 'db'
    if (!set_names_query.exec("SET NAMES 'utf8mb4'")) {
      qWarning() << "QtDbManager::openDatabase: Failed to execute SET NAMES "
                    "'utf8mb4' for connection"
                 << conn_name
                 << ". Error:" << set_names_query.lastError().text();
      // 根据需要决定这是否是致命错误。目前仅警告。
      // 如果是致命错误，可以关闭连接并返回错误：
      // db.close();
      // QSqlDatabase::removeDatabase(conn_name);
      // return std::unexpected(Error(ErrorCode::QueryExecutionError, "Failed to
      // set connection charset for " + conn_name.toStdString() + ": " +
      // set_names_query.lastError().text().toStdString()));
    } else {
      // qInfo() << "QtDbManager::openDatabase: Successfully executed SET NAMES
      // 'utf8mb4' for " << conn_name;
    }
  }
  return conn_name;
}

QSqlDatabase QtDbManager::getDatabase(const QString &connection_name) {
  return QSqlDatabase::database(connection_name);
}

void QtDbManager::closeDatabase(const QString &connection_name) {
  if (QSqlDatabase::contains(connection_name)) {
    QSqlDatabase db = QSqlDatabase::database(connection_name, false);
    if (db.isOpen()) {
      db.close();
    }
    QSqlDatabase::removeDatabase(connection_name);
  }
}

bool QtDbManager::isConnectionValid(const QString &connection_name) {
  if (!QSqlDatabase::contains(connection_name)) {
    return false;
  }
  QSqlDatabase db = QSqlDatabase::database(connection_name, false);
  return db.isValid() && db.isOpen();
}

} // namespace cpporm