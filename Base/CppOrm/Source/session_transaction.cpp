// cpporm/session_transaction.cpp
#include "cpporm/qt_db_manager.h"
#include "cpporm/session.h"

#include <QDebug>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>

namespace cpporm {

std::expected<std::unique_ptr<Session>, Error> Session::Begin() {
  if (is_explicit_transaction_handle_) {
    // GORM Go allows nested transactions using SAVEPOINT. QtSqlDatabase doesn't
    // directly expose SAVEPOINT. For simplicity, we disallow nested Begin() on
    // the same Session object conceptually. A new Session object representing a
    // savepoint would be a more advanced feature.
    qWarning() << "cpporm Session::Begin: Attempting to Begin() on an already "
                  "transactional Session. "
                  "This usually implies a logical error or need for savepoints "
                  "(not directly supported).";
    return std::unexpected(
        Error(ErrorCode::TransactionError,
              "Session is already explicitly transactional. Nested Begin() is "
              "not supported in this manner."));
  }

  // Ensure the current db_handle_ is valid and open.
  if (!db_handle_.isValid()) {
    return std::unexpected(Error(
        ErrorCode::ConnectionInvalid,
        "Cannot begin transaction: Session's QSqlDatabase handle is invalid."));
  }
  if (!db_handle_.isOpen()) {
    qInfo() << "Session::Begin: Database handle was not open. Attempting to "
               "open...";
    if (!db_handle_.open()) {
      QSqlError open_err = db_handle_.lastError();
      return std::unexpected(Error(ErrorCode::ConnectionNotOpen,
                                   "Failed to open database for transaction: " +
                                       open_err.text().toStdString(),
                                   open_err.nativeErrorCode().toInt()));
    }
  }

  if (!db_handle_.driver()) {
    return std::unexpected(
        Error(ErrorCode::InternalError,
              "Cannot begin transaction: QSqlDatabase driver is null."));
  }

  if (!db_handle_.driver()->hasFeature(QSqlDriver::Transactions)) {
    return std::unexpected(Error(ErrorCode::UnsupportedFeature,
                                 "Database driver for connection '" +
                                     connection_name_.toStdString() +
                                     "' does not support transactions."));
  }

  // Attempt to start the transaction on the current connection.
  if (db_handle_.transaction()) {
    // Create a new Session instance that "owns" this transaction.
    // It uses the same underlying connection (via a copy of db_handle_), but is
    // marked as transactional. The Session(QSqlDatabase) constructor sets
    // is_explicit_transaction_handle_ = true.
    return std::make_unique<Session>(db_handle_);
  } else {
    QSqlError q_error = db_handle_.lastError();
    return std::unexpected(
        Error(ErrorCode::TransactionError,
              "Failed to begin transaction on connection '" +
                  connection_name_.toStdString() +
                  "': " + q_error.text().toStdString() +
                  " (Driver Error: " + q_error.driverText().toStdString() +
                  ", DB Error: " + q_error.databaseText().toStdString() + ")",
              q_error.nativeErrorCode().toInt()));
  }
}

Error Session::Commit() {
  if (!is_explicit_transaction_handle_) {
    return Error(
        ErrorCode::TransactionError,
        "Commit called on a non-transactional Session. Call Begin() first.");
  }
  if (!db_handle_.isValid()) {               // Check if handle became invalid
    is_explicit_transaction_handle_ = false; // Reset state as it's broken
    return Error(ErrorCode::ConnectionInvalid,
                 "Cannot commit: QSqlDatabase handle is invalid.");
  }
  if (!db_handle_.isOpen()) {                // Check if connection dropped
    is_explicit_transaction_handle_ = false; // Reset state
    return Error(ErrorCode::ConnectionNotOpen,
                 "Cannot commit: Database connection is not open.");
  }
  if (!db_handle_.driver() ||
      !db_handle_.driver()->hasFeature(QSqlDriver::Transactions)) {
    is_explicit_transaction_handle_ = false;
    return Error(ErrorCode::UnsupportedFeature,
                 "Cannot commit: Driver does not support transactions or "
                 "driver is null.");
  }

  if (db_handle_.commit()) {
    is_explicit_transaction_handle_ = false; // Transaction successfully ended
    return make_ok();
  } else {
    QSqlError q_error = db_handle_.lastError();
    // After a failed commit, the transaction is typically still active (per SQL
    // standard). User should ideally Rollback. We don't automatically set
    // is_explicit_transaction_handle_ = false here.
    return Error(
        ErrorCode::TransactionError,
        "Failed to commit transaction: " + q_error.text().toStdString() +
            " (Driver: " + q_error.driverText().toStdString() +
            ", DB: " + q_error.databaseText().toStdString() + ")",
        q_error.nativeErrorCode().toInt());
  }
}

Error Session::Rollback() {
  if (!is_explicit_transaction_handle_) {
    return Error(
        ErrorCode::TransactionError,
        "Rollback called on a non-transactional Session. Call Begin() first.");
  }
  if (!db_handle_.isValid()) {
    is_explicit_transaction_handle_ = false;
    return Error(ErrorCode::ConnectionInvalid,
                 "Cannot rollback: QSqlDatabase handle is invalid.");
  }
  if (!db_handle_.isOpen()) {
    is_explicit_transaction_handle_ = false;
    return Error(ErrorCode::ConnectionNotOpen,
                 "Cannot rollback: Database connection is not open.");
  }
  if (!db_handle_.driver() ||
      !db_handle_.driver()->hasFeature(QSqlDriver::Transactions)) {
    is_explicit_transaction_handle_ = false;
    return Error(ErrorCode::UnsupportedFeature,
                 "Cannot rollback: Driver does not support transactions or "
                 "driver is null.");
  }

  if (db_handle_.rollback()) {
    is_explicit_transaction_handle_ = false; // Transaction successfully ended
    return make_ok();
  } else {
    QSqlError q_error = db_handle_.lastError();
    // Even if rollback fails, the transactional state is effectively over or
    // undefined.
    is_explicit_transaction_handle_ = false;
    return Error(
        ErrorCode::TransactionError,
        "Failed to rollback transaction: " + q_error.text().toStdString() +
            " (Driver: " + q_error.driverText().toStdString() +
            ", DB: " + q_error.databaseText().toStdString() + ")",
        q_error.nativeErrorCode().toInt());
  }
}

bool Session::IsTransaction() const {
  // is_explicit_transaction_handle_ is the primary indicator managed by this
  // Session class. Additionally, ensure the handle itself is still in a state
  // that could support a transaction.
  if (is_explicit_transaction_handle_) {
    if (db_handle_.isValid() && db_handle_.isOpen() && db_handle_.driver() &&
        db_handle_.driver()->hasFeature(QSqlDriver::Transactions)) {
      // Qt doesn't have a direct QSqlDatabase::isTransactionActive() method.
      // So, our flag is the best source of truth from the ORM's perspective
      // once Begin() has been successfully called on this Session object's
      // representative.
      return true;
    } else {
      // If our flag is true, but DB state is bad, it's an inconsistent state.
      // Logically, the Session *thinks* it's in a transaction.
      qWarning(
          "Session::IsTransaction: Session is marked as transactional, but DB "
          "handle is invalid, closed, or driver lost. Inconsistent state.");
      return true; // Or false, depending on how strictly to interpret. True
                   // means "was supposed to be".
    }
  }
  return false;
}

} // namespace cpporm