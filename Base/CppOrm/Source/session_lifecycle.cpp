// cpporm/session_lifecycle.cpp
#include "cpporm/model_base.h" // FriendAccess 可能需要 ModelBase/ModelMeta
#include "cpporm/qt_db_manager.h"
#include "cpporm/session.h" // 主头文件
#include "cpporm/session_priv_batch_helpers.h" // For FriendAccess definition & internal::SessionModelDataForWrite

#include <QDebug>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery> // For FriendAccess::callExecuteQueryInternal

namespace cpporm {

// --- Session 构造函数、析构函数、移动操作 ---
Session::Session(QString connection_name)
    : connection_name_(std::move(connection_name)),
      db_handle_(QtDbManager::getDatabase(connection_name_)),
      is_explicit_transaction_handle_(false),
      temp_on_conflict_clause_(nullptr) {
  if (!db_handle_.isValid()) {
    qWarning()
        << "cpporm Session: Constructed with invalid QSqlDatabase for "
           "connection name:"
        << connection_name_ << ". Last DB error: "
        << QSqlDatabase::database(connection_name_, false).lastError().text();
  }
}

Session::Session(QSqlDatabase db_handle)
    : connection_name_(db_handle.connectionName()), db_handle_(db_handle),
      is_explicit_transaction_handle_(true), temp_on_conflict_clause_(nullptr) {
  if (!db_handle_.isValid()) {
    qWarning() << "cpporm Session: Constructed with an invalid QSqlDatabase "
                  "handle for connection:"
               << connection_name_;
  }
}

Session::~Session() {
  if (is_explicit_transaction_handle_ && db_handle_.isValid() &&
      db_handle_.isOpen() && db_handle_.driver() &&
      db_handle_.driver()->hasFeature(QSqlDriver::Transactions)) {
    qWarning() << "cpporm Session: Destructor called for an active "
                  "transaction on connection"
               << connection_name_ << ". Rolling back automatically.";
    db_handle_.rollback();
  }
}

Session::Session(Session &&other) noexcept
    : connection_name_(std::move(other.connection_name_)),
      db_handle_(std::move(other.db_handle_)),
      is_explicit_transaction_handle_(other.is_explicit_transaction_handle_),
      temp_on_conflict_clause_(std::move(other.temp_on_conflict_clause_)) {
  other.is_explicit_transaction_handle_ = false;
}

Session &Session::operator=(Session &&other) noexcept {
  if (this != &other) {
    if (is_explicit_transaction_handle_ && db_handle_.isValid() &&
        db_handle_.isOpen() && db_handle_.driver() &&
        db_handle_.driver()->hasFeature(QSqlDriver::Transactions)) {
      db_handle_.rollback();
    }
    connection_name_ = std::move(other.connection_name_);
    db_handle_ = std::move(other.db_handle_);
    is_explicit_transaction_handle_ = other.is_explicit_transaction_handle_;
    temp_on_conflict_clause_ = std::move(other.temp_on_conflict_clause_);
    other.is_explicit_transaction_handle_ = false;
  }
  return *this;
}

// --- Accessors implementation ---
QString Session::getConnectionName() const { return connection_name_; }

QSqlDatabase Session::getDbHandle() const {
  if (!connection_name_.isEmpty()) {
    // Ensure it returns the handle for this session, not just a default
    return QSqlDatabase::database(connection_name_, false);
  }
  // If connection_name_ is empty (should not happen with current constructors),
  // or if the named connection was removed externally, this could be an issue.
  // For safety, one might re-validate db_handle_ here or always use
  // QSqlDatabase::database. However, db_handle_ is stored for transactional
  // session objects.
  return db_handle_; // Return the stored handle
}
const OnConflictClause *Session::getTempOnConflictClause() const {
  return temp_on_conflict_clause_.get();
}
void Session::clearTempOnConflictClause() { temp_on_conflict_clause_.reset(); }

// --- internal_batch_helpers::FriendAccess implementations ---
// 这些方法使得 internal_batch_helpers 命名空间中的函数能够以受控的方式访问
// Session 的私有成员。

cpporm::internal::SessionModelDataForWrite
internal_batch_helpers::FriendAccess::callExtractModelData(
    Session &s, const ModelBase &model_instance, const ModelMeta &meta,
    bool for_update, bool include_timestamps_even_if_null) {
  // 直接调用 Session 的私有成员函数
  return s.extractModelData(model_instance, meta, for_update,
                            include_timestamps_even_if_null);
}

std::pair<QSqlQuery, Error>
internal_batch_helpers::FriendAccess::callExecuteQueryInternal(
    QSqlDatabase db, // Session 实例不是必需的，因为原始函数是静态的
    const QString &sql, const QVariantList &params) {
  // 直接调用 Session 的私有静态成员函数
  return Session::execute_query_internal(db, sql, params);
}

void internal_batch_helpers::FriendAccess::callAutoSetTimestamps(
    Session &s, ModelBase &model_instance, const ModelMeta &meta,
    bool is_create_op) {
  s.autoSetTimestamps(model_instance, meta, is_create_op);
}

} // namespace cpporm