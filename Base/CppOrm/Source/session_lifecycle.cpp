// Base/CppOrm/Source/session_lifecycle.cpp
#include <QDebug>

#include "cpporm/session.h"
#include "cpporm/session_priv_batch_helpers.h"
#include "cpporm_sqldriver/sql_database.h"
#include "cpporm_sqldriver/sql_driver_manager.h"
#include "cpporm_sqldriver/sql_enums.h"
#include "cpporm_sqldriver/sql_error.h"
#include "cpporm_sqldriver/sql_query.h"

namespace cpporm {

    Session::Session(const std::string &connection_name) : connection_name_(connection_name), db_handle_(cpporm_sqldriver::SqlDriverManager::database(connection_name_, false)), is_explicit_transaction_handle_(false), temp_on_conflict_clause_(nullptr) {
        if (!db_handle_.isValid()) {
            cpporm_sqldriver::SqlError err = db_handle_.lastError();
            qWarning() << "cpporm Session: Constructed with invalid SqlDatabase for "
                          "connection name:"
                       << QString::fromStdString(connection_name_) << ". Last DB error: " << QString::fromStdString(err.text());
        }
    }

    Session::Session(cpporm_sqldriver::SqlDatabase &&db_handle_rval) : connection_name_(db_handle_rval.connectionName()), db_handle_(std::move(db_handle_rval)), is_explicit_transaction_handle_(true), temp_on_conflict_clause_(nullptr) {
        if (!db_handle_.isValid()) {
            qWarning() << "cpporm Session: Constructed with an invalid SqlDatabase "
                          "handle (rvalue) for connection:"
                       << QString::fromStdString(connection_name_);
        }
    }

    Session::~Session() {
        if (is_explicit_transaction_handle_ && db_handle_.isValid() && db_handle_.isOpen() && db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
            if (db_handle_.isTransactionActive()) {
                qWarning() << "cpporm Session: Destructor called for an active "
                              "transaction on connection"
                           << QString::fromStdString(connection_name_) << ". Rolling back automatically.";
                db_handle_.rollback();
            }
        }
    }

    Session::Session(Session &&other) noexcept : connection_name_(std::move(other.connection_name_)), db_handle_(std::move(other.db_handle_)), is_explicit_transaction_handle_(other.is_explicit_transaction_handle_), temp_on_conflict_clause_(std::move(other.temp_on_conflict_clause_)) {
        other.is_explicit_transaction_handle_ = false;
    }

    Session &Session::operator=(Session &&other) noexcept {
        if (this != &other) {
            if (is_explicit_transaction_handle_ && db_handle_.isValid() && db_handle_.isOpen() && db_handle_.hasFeature(cpporm_sqldriver::Feature::Transactions)) {
                if (db_handle_.isTransactionActive()) {
                    db_handle_.rollback();
                }
            }
            connection_name_ = std::move(other.connection_name_);
            db_handle_ = std::move(other.db_handle_);
            is_explicit_transaction_handle_ = other.is_explicit_transaction_handle_;
            temp_on_conflict_clause_ = std::move(other.temp_on_conflict_clause_);
            other.is_explicit_transaction_handle_ = false;
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

    cpporm::internal::SessionModelDataForWrite internal_batch_helpers::FriendAccess::callExtractModelData(Session &s, const ModelBase &model_instance, const ModelMeta &meta, bool for_update, bool include_timestamps_even_if_null) {
        return s.extractModelData(model_instance, meta, for_update, include_timestamps_even_if_null);
    }

    // Definition matching the unified declaration
    std::pair<cpporm_sqldriver::SqlQuery, Error> internal_batch_helpers::FriendAccess::callExecuteQueryInternal(cpporm_sqldriver::SqlDatabase &db_conn_ref, const std::string &sql, const std::vector<cpporm_sqldriver::SqlValue> &bound_params) {
        return Session::execute_query_internal(db_conn_ref, sql, bound_params);
    }

    void internal_batch_helpers::FriendAccess::callAutoSetTimestamps(Session &s, ModelBase &model_instance, const ModelMeta &meta, bool is_create_op) {
        s.autoSetTimestamps(model_instance, meta, is_create_op);
    }

}  // namespace cpporm