// Base/CppOrm/Source/session_transaction.cpp
#include <QDebug>

#include "cpporm/session.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_driver_manager.h"
#include "sqldriver/sql_enums.h"
#include "sqldriver/sql_error.h"

namespace cpporm {

    std::expected<std::unique_ptr<Session>, Error> Session::Begin() {
        if (is_explicit_transaction_handle_) {
            qWarning() << "cpporm Session::Begin: Attempting to Begin() on an already "
                          "transactional Session. This usually implies a logical error or need "
                          "for savepoints (not directly supported by Begin() for new Session).";
            return std::unexpected(Error(ErrorCode::TransactionError, "Session is already explicitly transactional. Nested Begin() to create a new Session is not supported. Use savepoints on the existing session if needed."));
        }

        if (!db_handle_.isValid()) {
            return std::unexpected(Error(ErrorCode::ConnectionInvalid, "Cannot begin transaction: Session's SqlDatabase handle is invalid."));
        }
        if (!db_handle_.isOpen()) {
            qInfo() << "Session::Begin: Database handle for connection '" << QString::fromStdString(db_handle_.connectionName()) << "' was not open. Attempting to open...";
            if (!db_handle_.open()) {
                cpporm_sqldriver::SqlError open_err = db_handle_.lastError();
                return std::unexpected(Error(ErrorCode::ConnectionNotOpen, "Failed to open database for transaction: " + open_err.text(), open_err.nativeErrorCodeNumeric()));
            }
        }

        if (!db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
            return std::unexpected(Error(ErrorCode::UnsupportedFeature, "Database driver for connection '" + connection_name_ + "' does not support transactions."));
        }

        // Use SqlDatabase::transaction() which delegates to ISqlDriver::beginTransaction()
        if (db_handle_.transaction()) {  // Changed from beginTransaction
            auto tx_session = std::make_unique<Session>(connection_name_);
            tx_session->is_explicit_transaction_handle_ = true;
            return tx_session;
        } else {
            cpporm_sqldriver::SqlError q_error = db_handle_.lastError();
            return std::unexpected(Error(ErrorCode::TransactionError, "Failed to begin transaction on connection '" + connection_name_ + "': " + q_error.text() + " (Driver Text: " + q_error.driverText() + ", DB Text: " + q_error.databaseText() + ")", q_error.nativeErrorCodeNumeric()));
        }
    }

    Error Session::Commit() {
        if (!is_explicit_transaction_handle_) {
            return Error(ErrorCode::TransactionError, "Commit called on a non-transactional Session. Call Begin() first.");
        }
        if (!db_handle_.isValid()) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::ConnectionInvalid, "Cannot commit: SqlDatabase handle is invalid.");
        }
        if (!db_handle_.isOpen()) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::ConnectionNotOpen, "Cannot commit: Database connection is not open.");
        }
        if (!db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::UnsupportedFeature, "Cannot commit: Driver does not support transactions.");
        }

        if (db_handle_.commit()) {
            is_explicit_transaction_handle_ = false;
            return make_ok();
        } else {
            cpporm_sqldriver::SqlError q_error = db_handle_.lastError();
            return Error(ErrorCode::TransactionError, "Failed to commit transaction: " + q_error.text() + " (Driver: " + q_error.driverText() + ", DB: " + q_error.databaseText() + ")", q_error.nativeErrorCodeNumeric());
        }
    }

    Error Session::Rollback() {
        if (!is_explicit_transaction_handle_) {
            return Error(ErrorCode::TransactionError, "Rollback called on a non-transactional Session. Call Begin() first.");
        }
        if (!db_handle_.isValid()) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::ConnectionInvalid, "Cannot rollback: SqlDatabase handle is invalid.");
        }
        if (!db_handle_.isOpen()) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::ConnectionNotOpen, "Cannot rollback: Database connection is not open.");
        }
        if (!db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::UnsupportedFeature, "Cannot rollback: Driver does not support transactions.");
        }

        if (db_handle_.rollback()) {
            is_explicit_transaction_handle_ = false;
            return make_ok();
        } else {
            cpporm_sqldriver::SqlError q_error = db_handle_.lastError();
            is_explicit_transaction_handle_ = false;  // Set to false even on rollback failure
            return Error(ErrorCode::TransactionError, "Failed to rollback transaction: " + q_error.text() + " (Driver: " + q_error.driverText() + ", DB: " + q_error.databaseText() + ")", q_error.nativeErrorCodeNumeric());
        }
    }

    bool Session::IsTransaction() const {
        if (is_explicit_transaction_handle_) {
            if (db_handle_.isValid() && db_handle_.isOpen() && db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
                return db_handle_.isTransactionActive();
            } else {
                qWarning(
                    "Session::IsTransaction: Session is marked as transactional, but DB "
                    "handle is invalid, closed, or driver lost. Inconsistent state.");
                return false;
            }
        }
        return false;
    }

}  // namespace cpporm