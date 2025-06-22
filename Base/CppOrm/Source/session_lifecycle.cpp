// Base/CppOrm/Source/session_lifecycle.cpp
#include <QDebug>  // For qWarning

#include "cpporm/session.h"
#include "cpporm/session_priv_batch_helpers.h"  // For FriendAccess definition
#include "sqldriver/sql_database.h"             // For cpporm_sqldriver::SqlDatabase
#include "sqldriver/sql_driver_manager.h"       // For defaultConnectionName if needed (less likely now)
#include "sqldriver/sql_enums.h"                // For cpporm_sqldriver::Feature
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_query.h"  // For execute_query_internal

namespace cpporm {

    // Constructor now takes an rvalue reference to an SqlDatabase object
    Session::Session(cpporm_sqldriver::SqlDatabase &&db_handle_rval)
        : connection_name_(db_handle_rval.connectionName()),  // Get name before db_handle_rval is moved
          db_handle_(std::move(db_handle_rval)),
          is_explicit_transaction_handle_(false),  // A regular session is not initially transactional
          temp_on_conflict_clause_(nullptr) {
        if (!db_handle_.isValid()) {
            qWarning() << "cpporm Session: Constructed with an invalid SqlDatabase "
                          "handle for connection:"
                       << QString::fromStdString(connection_name_);
        } else if (!db_handle_.isOpen()) {
            qWarning() << "cpporm Session: Constructed with a valid but NOT OPEN SqlDatabase "
                          "handle for connection:"
                       << QString::fromStdString(connection_name_);
        }
    }

    Session::~Session() {
        // If this session represents an explicit transaction that wasn't committed/rolled back,
        // it should be rolled back.
        if (is_explicit_transaction_handle_ && db_handle_.isValid() && db_handle_.isOpen() && db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
            if (db_handle_.isTransactionActive()) {  // isTransactionActive() should be on SqlDatabase
                qWarning() << "cpporm Session: Destructor called for an active "
                              "transaction on connection"
                           << QString::fromStdString(connection_name_) << ". Rolling back automatically.";
                db_handle_.rollback();  // SqlDatabase::rollback()
            }
        }
        // db_handle_ (SqlDatabase) destructor will handle closing its connection if it's still open
        // and freeing its ISqlDriver if it owns it.
    }

    Session::Session(Session &&other) noexcept : connection_name_(std::move(other.connection_name_)), db_handle_(std::move(other.db_handle_)), is_explicit_transaction_handle_(other.is_explicit_transaction_handle_), temp_on_conflict_clause_(std::move(other.temp_on_conflict_clause_)) {
        other.is_explicit_transaction_handle_ = false;  // Reset moved-from session's tx state
    }

    Session &Session::operator=(Session &&other) noexcept {
        if (this != &other) {
            // Clean up current session's resources if it's managing an explicit transaction
            if (is_explicit_transaction_handle_ && db_handle_.isValid() && db_handle_.isOpen() && db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
                if (db_handle_.isTransactionActive()) {
                    db_handle_.rollback();
                }
            }
            // db_handle_ old resources will be cleaned up by its move assignment or destructor if it's not moved from.

            connection_name_ = std::move(other.connection_name_);
            db_handle_ = std::move(other.db_handle_);  // SqlDatabase move assignment
            is_explicit_transaction_handle_ = other.is_explicit_transaction_handle_;
            temp_on_conflict_clause_ = std::move(other.temp_on_conflict_clause_);

            other.is_explicit_transaction_handle_ = false;  // Reset moved-from session's tx state
        }
        return *this;
    }

    const std::string &Session::getConnectionName() const {
        return connection_name_;
    }

    cpporm_sqldriver::SqlDatabase &Session::getDbHandle() {
        return db_handle_;
    }
    const cpporm_sqldriver::SqlDatabase &Session::getDbHandle() const {
        return db_handle_;
    }

    const OnConflictClause *Session::getTempOnConflictClause() const {
        return temp_on_conflict_clause_.get();
    }

    void Session::clearTempOnConflictClause() {
        temp_on_conflict_clause_.reset();
    }

    // FriendAccess static method definitions
    // These allow internal_batch_helpers to call private methods of Session
    cpporm::internal::SessionModelDataForWrite internal_batch_helpers::FriendAccess::callExtractModelData(Session &s, const ModelBase &model_instance, const ModelMeta &meta, bool for_update, bool include_timestamps_even_if_null) {
        return s.extractModelData(model_instance, meta, for_update, include_timestamps_even_if_null);
    }

    std::pair<cpporm_sqldriver::SqlQuery, Error> internal_batch_helpers::FriendAccess::callExecuteQueryInternal(cpporm_sqldriver::SqlDatabase &db_conn_ref, const std::string &sql, const std::vector<cpporm_sqldriver::SqlValue> &bound_params) {
        // Session::execute_query_internal is already public static, so FriendAccess isn't strictly needed for this one if Session has it public static.
        // However, if it were private, this friend access would be necessary.
        return Session::execute_query_internal(db_conn_ref, sql, bound_params);
    }

    void internal_batch_helpers::FriendAccess::callAutoSetTimestamps(Session &s, ModelBase &model_instance, const ModelMeta &meta, bool is_create_op) {
        s.autoSetTimestamps(model_instance, meta, is_create_op);
    }

}  // namespace cpporm