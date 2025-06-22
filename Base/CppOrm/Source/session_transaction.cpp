// Base/CppOrm/Source/session_transaction.cpp
#include <QDebug>  // For qWarning

#include "cpporm/session.h"
#include "sqldriver/sql_database.h"  // For cpporm_sqldriver::SqlDatabase
#include "sqldriver/sql_enums.h"     // For cpporm_sqldriver::Feature
#include "sqldriver/sql_error.h"

namespace cpporm {

    std::expected<std::unique_ptr<Session>, Error> Session::Begin() {
        if (is_explicit_transaction_handle_) {
            // This session instance itself is already managing an explicit transaction.
            // Returning a new unique_ptr to this same instance doesn't make sense with unique_ptr semantics
            // and the user's code pattern `auto tx_session = std::move(expected.value())`.
            return std::unexpected(Error(ErrorCode::TransactionError, "Session is already explicitly managing a transaction. Nested Begin() to create a new Session wrapper for the same transaction is not standard."));
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

        if (db_handle_.isTransactionActive()) {
            return std::unexpected(Error(ErrorCode::TransactionError, "Transaction is already active on the underlying connection. Nested Begin() creating a new Session wrapper is ambiguous."));
        }

        if (db_handle_.transaction()) {  // This starts a transaction on this->db_handle_
            // The challenge: how to create a new Session that uses this *same* underlying
            // ISqlDriver / MYSQL* which is now in a transaction.
            // SqlDatabase owns ISqlDriver uniquely.

            // Strategy 1: Move the handle (invalidates original session for DB ops)
            // Create a temporary SqlDatabase by moving from the current session's handle.
            // This means 'this->db_handle_' becomes invalid after the move.
            cpporm_sqldriver::SqlDatabase transactional_db_handle = std::move(this->db_handle_);

            // Create a new Session instance, moving the transactional_db_handle into it.
            auto tx_session = std::make_unique<Session>(std::move(transactional_db_handle));
            tx_session->is_explicit_transaction_handle_ = true;  // Mark this new session as managing the transaction

            qWarning() << "Session::Begin(): Original session's database handle has been moved to the new transactional session. The original session should not be used for further database operations.";

            return tx_session;

            // Strategy 2 (Requires SqlDatabase/ISqlDriver redesign for shared ownership or non-owning wrappers):
            // Create a new SqlDatabase that refers to the *same* ISqlDriver (e.g. via shared_ptr or non-owning raw ptr)
            // auto tx_session = std::make_unique<Session>( SqlDatabase(...) /* constructor taking shared driver */ );
            // tx_session->is_explicit_transaction_handle_ = true;
            // return tx_session;
            // This is a larger change not undertaken here.

            // Strategy 3 (Simpler for user, but current session becomes the transaction manager):
            // this->is_explicit_transaction_handle_ = true;
            // return std::make_unique<Session>(*this); // This is problematic with unique_ptr and ownership
            // The pattern `auto tx_session = std::move(expected.value())` suggests a new distinct object.

        } else {
            cpporm_sqldriver::SqlError q_error = db_handle_.lastError();  // Get error from the original db_handle_
            return std::unexpected(Error(ErrorCode::TransactionError, "Failed to begin transaction on connection '" + connection_name_ + "': " + q_error.text(), q_error.nativeErrorCodeNumeric()));
        }
    }

    Error Session::Commit() {
        if (!is_explicit_transaction_handle_) {
            return Error(ErrorCode::TransactionError, "Commit called on a Session not managing an explicit transaction. Call Begin() first on a new Session or ensure this Session was returned by Begin().");
        }
        if (!db_handle_.isValid()) {
            is_explicit_transaction_handle_ = false;  // No longer managing if handle invalid
            return Error(ErrorCode::ConnectionInvalid, "Cannot commit: SqlDatabase handle is invalid.");
        }
        // isOpen check is implicitly handled by db_handle_.commit()
        if (!db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::UnsupportedFeature, "Cannot commit: Driver does not support transactions.");
        }
        if (!db_handle_.isTransactionActive()) {
            is_explicit_transaction_handle_ = false;  // Transaction was already ended.
            return Error(ErrorCode::TransactionError, "Cannot commit: No active transaction on the underlying connection.");
        }

        if (db_handle_.commit()) {
            is_explicit_transaction_handle_ = false;  // Transaction ended
            return make_ok();
        } else {
            cpporm_sqldriver::SqlError q_error = db_handle_.lastError();
            // Transaction might still be active after a failed commit attempt depending on DB.
            // For safety, we mark this Session as no longer managing it explicitly.
            // The underlying db_handle_.isTransactionActive() would tell the true state.
            // is_explicit_transaction_handle_ = false; // Consider if this should be reset on failure
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
        if (!db_handle_.isTransactionActive()) {
            is_explicit_transaction_handle_ = false;
            return Error(ErrorCode::TransactionError, "Cannot rollback: No active transaction on the underlying connection.");
        }

        if (db_handle_.rollback()) {
            is_explicit_transaction_handle_ = false;  // Transaction ended
            return make_ok();
        } else {
            cpporm_sqldriver::SqlError q_error = db_handle_.lastError();
            // is_explicit_transaction_handle_ = false; // Consider resetting on failure
            return Error(ErrorCode::TransactionError, "Failed to rollback transaction: " + q_error.text(), q_error.nativeErrorCodeNumeric());
        }
    }

    bool Session::IsTransaction() const {
        // This method now reflects if *this Session instance* is the one
        // that was created by Begin() and is responsible for Commit/Rollback.
        // The actual transaction state is on db_handle_.
        if (is_explicit_transaction_handle_) {
            if (db_handle_.isValid() && db_handle_.isOpen()) {  // Ensure handle is usable
                return db_handle_.isTransactionActive();
            }
            // If handle is not valid/open but Session thinks it's transactional, it's an inconsistent state
            return false;
        }
        return false;
    }

}  // namespace cpporm