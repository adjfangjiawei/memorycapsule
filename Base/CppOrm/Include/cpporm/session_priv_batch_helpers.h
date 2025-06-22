// Base/CppOrm/Include/cpporm/session_priv_batch_helpers.h
#ifndef cpporm_SESSION_PRIV_BATCH_HELPERS_H
#define cpporm_SESSION_PRIV_BATCH_HELPERS_H

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

#include "cpporm/error.h"
#include "cpporm/session_fwd.h"
#include "cpporm/session_types.h"
#include "sqldriver/sql_query.h"
// Forward declare types from cpporm_sqldriver that are used in signatures
namespace cpporm_sqldriver {
    class SqlQuery;
    class SqlValue;
    class SqlDatabase;
}  // namespace cpporm_sqldriver

namespace cpporm {

    class ModelBase;
    struct ModelMeta;
    struct FieldMeta;
    struct OnConflictClause;
    class QueryBuilder;

    namespace internal_batch_helpers {

        class FriendAccess {
          public:
            static internal::SessionModelDataForWrite callExtractModelData(Session &s, const ModelBase &model_instance, const ModelMeta &meta, bool for_update, bool include_timestamps_even_if_null);

            // Unified parameter names with definition
            static std::pair<cpporm_sqldriver::SqlQuery, Error> callExecuteQueryInternal(cpporm_sqldriver::SqlDatabase &db_conn_ref, const std::string &sql, const std::vector<cpporm_sqldriver::SqlValue> &bound_params);

            static void callAutoSetTimestamps(Session &s, ModelBase &model_instance, const ModelMeta &meta, bool is_create_op);
        };

        struct BatchSqlParts {
            QString sql_insert_base;
            QStringList row_placeholders;
            std::vector<cpporm_sqldriver::SqlValue> all_values_flattened;
            QString sql_on_conflict_suffix;
            QVariantList conflict_suffix_bindings;
            std::string final_sql_statement;
            std::vector<cpporm_sqldriver::SqlValue> final_bindings;
            bool can_proceed = false;
        };

        struct ExecutionResult {
            std::optional<cpporm_sqldriver::SqlQuery> query_object_opt;
            long long rows_affected = -1;
            Error db_error = make_ok();
            std::vector<ModelBase *> models_potentially_persisted;

            ExecutionResult() = default;
            ExecutionResult(ExecutionResult &&) = default;
            ExecutionResult &operator=(ExecutionResult &&) = default;
            ExecutionResult(const ExecutionResult &) = delete;
            ExecutionResult &operator=(const ExecutionResult &) = delete;
        };

        std::pair<std::vector<ModelBase *>, Error> prepareModelsAndSqlPlaceholders(Session &session, const std::vector<ModelBase *> &models_in_provider_chunk, const ModelMeta &meta, const std::vector<std::string> &batch_ordered_db_field_names_cache, BatchSqlParts &out_sql_parts);

        Error buildFullBatchSqlStatement(const Session &session, const QueryBuilder &qb_prototype, const ModelMeta &meta, const std::vector<std::string> &batch_ordered_db_field_names_cache, const OnConflictClause *active_conflict_clause, BatchSqlParts &in_out_sql_parts);

        ExecutionResult executeBatchSql(Session &session, const std::string &sql_to_execute_std, const std::vector<cpporm_sqldriver::SqlValue> &bindings_sqlvalue, const std::vector<ModelBase *> &models_in_db_op, const OnConflictClause *active_conflict_clause);

        std::vector<ModelBase *> backfillIdsFromReturning(cpporm_sqldriver::SqlQuery &executed_query, const ModelMeta &meta, const std::vector<ModelBase *> &models_to_backfill_from, const std::string &pk_cpp_name_str, const std::type_index &pk_cpp_type);

        std::vector<ModelBase *> backfillIdsFromLastInsertId(cpporm_sqldriver::SqlQuery &executed_query,
                                                             const Session &session,
                                                             const ModelMeta &meta,
                                                             const std::vector<ModelBase *> &models_to_backfill_from,
                                                             long long total_rows_affected_by_query,
                                                             const std::string &pk_cpp_name_str,
                                                             const std::type_index &pk_cpp_type,
                                                             const OnConflictClause *active_conflict_clause);

        void callAfterCreateHooks(Session &session, const std::vector<ModelBase *> &successfully_persisted_and_backfilled_models, Error &in_out_first_error_encountered);

    }  // namespace internal_batch_helpers
}  // namespace cpporm

#endif