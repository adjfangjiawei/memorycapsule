// Base/CppOrm/Source/session_batch_execution_and_hooks.cpp
#include <QDebug>
#include <QVariant>

#include "cpporm/session.h"
#include "cpporm/session_priv_batch_helpers.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_query.h"
#include "sqldriver/sql_value.h"

namespace cpporm {
    namespace internal_batch_helpers {

        ExecutionResult executeBatchSql(Session &session, const std::string &sql_to_execute_std, const std::vector<cpporm_sqldriver::SqlValue> &bindings_sqlvalue, const std::vector<ModelBase *> &models_in_db_op, const OnConflictClause *active_conflict_clause) {
            ExecutionResult result;

            auto exec_pair = FriendAccess::callExecuteQueryInternal(session.getDbHandle(), sql_to_execute_std, bindings_sqlvalue);

            // Emplace the SqlQuery object into the optional
            // SqlQuery must be constructible/movable for this.
            // We emplace it regardless of error because SqlQuery object itself might contain more error details
            // or its state might be 'invalid' which is fine.
            result.query_object_opt.emplace(std::move(exec_pair.first));
            result.db_error = exec_pair.second;

            if (result.db_error) {
                // Even if there's an error, numRowsAffected might be relevant or -1
                if (result.query_object_opt && result.query_object_opt->isValid()) {
                    result.rows_affected = result.query_object_opt->numRowsAffected();
                }
                return result;  // Return early on DB error from execution
            }

            // If no DB error, query_object_opt should have a value and be valid
            if (result.query_object_opt && result.query_object_opt->isValid()) {
                result.rows_affected = result.query_object_opt->numRowsAffected();
            } else {
                // This case should ideally be covered by result.db_error if query object is not valid post-execution
                qWarning() << "executeBatchSql: SqlQuery object is not valid after successful-flagged execution.";
                result.rows_affected = -1;     // Indicate error or unknown state
                if (result.db_error.isOk()) {  // If no error was previously set, set one now.
                    result.db_error = Error(ErrorCode::QueryExecutionError, "SQLQuery object invalid post-execution without prior error.");
                }
                return result;
            }

            if (result.rows_affected > 0 || (active_conflict_clause && active_conflict_clause->action != OnConflictClause::Action::DoNothing && result.rows_affected >= 0)) {
                for (ModelBase *m : models_in_db_op) {
                    if (m) {
                        m->_is_persisted = true;
                        result.models_potentially_persisted.push_back(m);
                    }
                }
            } else if (result.rows_affected == 0 && active_conflict_clause && active_conflict_clause->action == OnConflictClause::Action::DoNothing) {
                for (ModelBase *m : models_in_db_op) {
                    if (m) {
                        result.models_potentially_persisted.push_back(m);
                    }
                }
            }
            return result;  // NRVO should handle this (result is local)
        }

        void callAfterCreateHooks(Session &session, const std::vector<ModelBase *> &models_for_hooks, Error &in_out_first_error_encountered) {
            for (ModelBase *model_ptr : models_for_hooks) {
                if (!model_ptr || !model_ptr->_is_persisted) {
                    continue;
                }

                Error hook_err = model_ptr->afterCreate(session);
                if (hook_err) {
                    if (in_out_first_error_encountered.isOk()) {
                        in_out_first_error_encountered = hook_err;
                    }
                    qWarning() << "callAfterCreateHooks: afterCreate hook failed for a model (table: " << QString::fromStdString(model_ptr->_getTableName()) << "). Error: " << QString::fromStdString(hook_err.toString());
                }
            }
        }

    }  // namespace internal_batch_helpers
}  // namespace cpporm