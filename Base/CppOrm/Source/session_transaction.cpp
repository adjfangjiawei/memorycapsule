// Base/CppOrm/Source/session_transaction.cpp
#include <QDebug>  // For qWarning

#include "cpporm/session.h"
#include "sqldriver/sql_database.h"  // For cpporm_sqldriver::SqlDatabase
#include "sqldriver/sql_enums.h"     // For cpporm_sqldriver::Feature
#include "sqldriver/sql_error.h"

namespace cpporm {

    std::expected<std::unique_ptr<Session>, Error> Session::Begin() {
        if (is_explicit_transaction_handle_) {
            return std::unexpected(Error(ErrorCode::TransactionError, "Session is already explicitly managing a transaction. Nested Begin() is not supported by creating a new Session wrapper."));
        }

        if (!db_handle_.isValid()) {
            return std::unexpected(Error(ErrorCode::ConnectionInvalid, "Cannot begin transaction: Session's SqlDatabase handle is invalid."));
        }
        if (!db_handle_.isOpen()) {
            qInfo() << "Session::Begin: Database handle for connection '" << QString::fromStdString(db_handle_.connectionName()) << "' was not open. Attempting to open...";
            if (!db_handle_.open()) {  // Assuming db_handle_ has its params cached
                cpporm_sqldriver::SqlError open_err = db_handle_.lastError();
                return std::unexpected(Error(ErrorCode::ConnectionNotOpen, "Failed to open database for transaction: " + open_err.text(), open_err.nativeErrorCodeNumeric()));
            }
        }

        if (!db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
            return std::unexpected(Error(ErrorCode::UnsupportedFeature, "Database driver for connection '" + connection_name_ + "' does not support transactions."));
        }

        // Check if a transaction is ALREADY active on this shared connection,
        // which might indicate an issue if Begin() is called multiple times without
        // corresponding Commit/Rollback on the same logical connection flow.
        if (db_handle_.isTransactionActive()) {  // Relies on SqlDatabase::isTransactionActive() querying driver
            qWarning() << "Session::Begin: A transaction is already active on the underlying connection '" << QString::fromStdString(connection_name_) << "'. Proceeding to start a new logical transaction Session wrapper.";
            // Depending on DB and driver, `db_handle_.transaction()` might start a nested transaction
            // or be a no-op if already in a transaction, or error.
            // MySQL default behavior with START TRANSACTION inside another is to commit the previous one
            // implicitly, unless savepoints are used. This needs careful handling.
            // For now, we proceed, and `db_handle_.transaction()` will attempt its action.
        }

        if (db_handle_.transaction()) {  // This starts a transaction on this->db_handle_
            // Create a new SqlDatabase object that SHARES the same underlying ISqlDriver.
            // This is now possible because SqlDatabase uses shared_ptr and has a copy constructor.
            cpporm_sqldriver::SqlDatabase transactional_db_handle(this->db_handle_);  // Copy constructs, shares m_driver

            // Create a new Session instance, moving the new SqlDatabase (which shares the driver) into it.
            auto tx_session = std::make_unique<Session>(std::move(transactional_db_handle));
            tx_session->is_explicit_transaction_handle_ = true;  // Mark this new session as managing the transaction

            // The original session 'this' remains valid and usable because its db_handle_
            // still holds a valid shared_ptr to the ISqlDriver.
            return tx_session;
        } else {
            cpporm_sqldriver::SqlError q_error = db_handle_.lastError();
            return std::unexpected(Error(ErrorCode::TransactionError, "Failed to begin transaction on connection '" + connection_name_ + "': " + q_error.text(), q_error.nativeErrorCodeNumeric()));
        }
    }

    // Commit, Rollback, IsTransaction methods remain largely the same as the previous fix,
    // relying on is_explicit_transaction_handle_ and the db_handle_'s state.
    Error Session::Commit() {
        if (!is_explicit_transaction_handle_) {
            return Error(ErrorCode::TransactionError, "Commit called on a Session not managing an explicit transaction. Ensure this Session was returned by Begin().");
        }
        if (!db_handle_.isValid()) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::ConnectionInvalid, "Cannot commit: SqlDatabase handle is invalid.");
        }
        if (!db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::UnsupportedFeature, "Cannot commit: Driver does not support transactions.");
        }
        // No need to check db_handle_.isTransactionActive() before calling db_handle_.commit(),
        // as db_handle_.commit() should handle the case where no transaction is active (e.g., by returning false and setting an error).

        if (db_handle_.commit()) {
            is_explicit_transaction_handle_ = false;
            return make_ok();
        } else {
            cpporm_sqldriver::SqlError q_error = db_handle_.lastError();
            // Do not reset is_explicit_transaction_handle_ on commit failure,
            // as the transaction might still be technically active and require a rollback.
            return Error(ErrorCode::TransactionError, "Failed to commit transaction: " + q_error.text(), q_error.nativeErrorCodeNumeric());
        }
    }

    Error Session::Rollback() {
        if (!is_explicit_transaction_handle_) {
            return Error(ErrorCode::TransactionError, "Rollback called on a Session not managing an explicit transaction.");
        }
        if (!db_handle_.isValid()) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::ConnectionInvalid, "Cannot rollback: SqlDatabase handle is invalid.");
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
            is_explicit_transaction_handle_ = false;  // Even on rollback failure, this Session wrapper is done.
            return Error(ErrorCode::TransactionError, "Failed to rollback transaction: " + q_error.text(), q_error.nativeErrorCodeNumeric());
        }
    }

    bool Session::IsTransaction() const {
        if (is_explicit_transaction_handle_) {
            if (db_handle_.isValid() && db_handle_.isOpen()) {
                // Relies on SqlDatabase::isTransactionActive accurately querying the driver
                return db_handle_.isTransactionActive();
            }
            return false;
        }
        return false;
    }

}  // namespace cpporm