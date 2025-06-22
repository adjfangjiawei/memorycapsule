// Base/CppOrm/Include/cpporm/session_core.h
#ifndef cpporm_SESSION_CORE_H
#define cpporm_SESSION_CORE_H

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <any>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "cpporm/error.h"
#include "cpporm/i_query_executor.h"
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session_fwd.h"
#include "cpporm/session_types.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_driver_manager.h"
#include "sqldriver/sql_query.h"
#include "sqldriver/sql_value.h"

namespace cpporm {
    namespace internal_batch_helpers {
        class FriendAccess;
    }
}  // namespace cpporm

namespace cpporm {

    class Session : public IQueryExecutor {
      public:
        explicit Session(const std::string &connection_name);
        explicit Session(cpporm_sqldriver::SqlDatabase &&db_handle_rval);

        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;
        Session(Session &&other) noexcept;
        Session &operator=(Session &&other) noexcept;
        ~Session();

        QueryBuilder Model(const ModelBase *model_instance_hint);
        QueryBuilder Model(const ModelMeta &meta);
        template <typename T>
        QueryBuilder Model() {
            static_assert(std::is_base_of<ModelBase, T>::value, "T must be a descendant of cpporm::ModelBase");
            return QueryBuilder(this, connection_name_, &(T::getModelMeta()));
        }
        QueryBuilder Table(const std::string &table_name);
        QueryBuilder MakeQueryBuilder();

        Session &OnConflictUpdateAllExcluded();
        Session &OnConflictDoNothing();
        Session &OnConflictUpdateSpecific(std::function<void(SessionOnConflictUpdateSetter &)> updater_fn);

        Error FirstImpl(const QueryBuilder &qb, ModelBase &result_model) override;
        Error FindImpl(const QueryBuilder &qb, std::vector<std::unique_ptr<ModelBase>> &results_vector, std::function<std::unique_ptr<ModelBase>()> element_type_factory) override;
        std::expected<cpporm_sqldriver::SqlValue, Error> CreateImpl(const QueryBuilder &qb, ModelBase &model, const OnConflictClause *conflict_options_override) override;
        std::expected<long long, Error> UpdatesImpl(const QueryBuilder &qb, const std::map<std::string, QueryValue> &updates) override;
        std::expected<long long, Error> DeleteImpl(const QueryBuilder &qb) override;
        std::expected<long long, Error> SaveImpl(const QueryBuilder &qb, ModelBase &model) override;
        std::expected<int64_t, Error> CountImpl(const QueryBuilder &qb) override;

        std::expected<cpporm_sqldriver::SqlValue, Error> Create(ModelBase &model, const OnConflictClause *conflict_options_override = nullptr);

        template <typename TModel>
        std::expected<cpporm_sqldriver::SqlValue, Error> Create(TModel &model);

        Error First(ModelBase &result_model);
        Error First(ModelBase &result_model, const QueryValue &primary_key_value);
        Error First(ModelBase &result_model, const std::vector<QueryValue> &primary_key_values);
        Error First(ModelBase &result_model, const std::map<std::string, QueryValue> &conditions);

        Error Find(std::vector<std::unique_ptr<ModelBase>> &results_vector, std::function<std::unique_ptr<ModelBase>()> element_type_factory);
        Error Find(std::vector<std::unique_ptr<ModelBase>> &results_vector, std::function<std::unique_ptr<ModelBase>()> element_type_factory, const std::map<std::string, QueryValue> &conditions);
        Error Find(std::vector<std::unique_ptr<ModelBase>> &results_vector, std::function<std::unique_ptr<ModelBase>()> element_type_factory, const std::string &query_string, const std::vector<QueryValue> &args = {});

        template <typename T>
        Error First(T *result_model, QueryBuilder qb);
        template <typename T>
        Error First(T *result_model);
        template <typename T>
        Error First(T *result_model, const QueryValue &primary_key_value);
        template <typename T>
        Error First(T *result_model, const std::vector<QueryValue> &primary_key_values);
        template <typename T>
        Error First(T *result_model, const std::map<std::string, QueryValue> &conditions);

        template <typename T>
        Error Find(std::vector<T> *results_vector, QueryBuilder qb);
        template <typename T>
        Error Find(std::vector<T> *results_vector);
        template <typename T>
        Error Find(std::vector<T> *results_vector, const std::map<std::string, QueryValue> &conditions);
        template <typename T>
        Error Find(std::vector<T> *results_vector, const std::string &query_string, const std::vector<QueryValue> &args = {});

        template <typename T>
        Error Find(std::vector<std::unique_ptr<T>> *results_vector, QueryBuilder qb);
        template <typename T>
        Error Find(std::vector<std::unique_ptr<T>> *results_vector);
        template <typename T>
        Error Find(std::vector<std::unique_ptr<T>> *results_vector, const std::map<std::string, QueryValue> &conditions);
        template <typename T>
        Error Find(std::vector<std::unique_ptr<T>> *results_vector, const std::string &query_string, const std::vector<QueryValue> &args = {});

        template <typename ModelType>
        std::expected<std::vector<std::shared_ptr<ModelType>>, Error> CreateBatch(const std::vector<ModelType *> &models, size_t internal_db_batch_size = 100, const OnConflictClause *conflict_options_override = nullptr);

        template <typename ModelType>
        std::expected<std::vector<std::shared_ptr<ModelType>>, Error> CreateBatch(std::vector<std::unique_ptr<ModelType>> &models, size_t internal_db_batch_size = 100, const OnConflictClause *conflict_options_override = nullptr);

        template <typename ModelType>
        std::expected<std::vector<std::shared_ptr<ModelType>>, Error> CreateBatch(const std::vector<std::shared_ptr<ModelType>> &models, size_t internal_db_batch_size = 100, const OnConflictClause *conflict_options_override = nullptr);

        template <typename ModelType>
        std::expected<std::vector<std::shared_ptr<ModelType>>, Error> CreateBatch(std::function<std::optional<std::vector<ModelType *>>()> data_batch_provider_typed, const OnConflictClause *conflict_options_override = nullptr, size_t internal_db_batch_processing_size_hint = 100);

        std::expected<size_t, Error> CreateBatchWithMeta(const ModelMeta &meta, const std::vector<ModelBase *> &models, size_t internal_batch_processing_size = 100, const OnConflictClause *conflict_options_override = nullptr);

        Error CreateBatchProviderInternal(QueryBuilder qb_prototype,
                                          std::function<std::optional<std::vector<ModelBase *>>()> data_batch_provider_base,
                                          std::function<void(const std::vector<ModelBase *> &processed_batch_models_with_ids, Error batch_error)> per_db_batch_completion_callback,
                                          const OnConflictClause *conflict_options_override);

        std::expected<long long, Error> Save(ModelBase &model);
        template <typename TModel>
        std::expected<long long, Error> Save(TModel &model);

        std::expected<long long, Error> Updates(QueryBuilder qb, const std::map<std::string, QueryValue> &updates);
        std::expected<long long, Error> Delete(QueryBuilder qb);

        std::expected<long long, Error> Updates(const ModelMeta &meta, const std::map<std::string, QueryValue> &updates, const std::map<std::string, QueryValue> &conditions);
        std::expected<long long, Error> Updates(const ModelBase &model_condition, const std::map<std::string, QueryValue> &updates);

        std::expected<long long, Error> Delete(const ModelBase &model_condition);
        std::expected<long long, Error> Delete(const ModelMeta &meta, const std::map<std::string, QueryValue> &conditions);

        std::expected<long long, Error> DeleteBatch(const ModelMeta &meta, const std::vector<std::map<std::string, QueryValue>> &primary_keys_list, size_t batch_delete_size = 100);

        std::expected<long long, Error> ExecRaw(const QString &sql, const QVariantList &args = {});

        Error AutoMigrate(const ModelMeta &meta);
        Error AutoMigrate(const std::vector<const ModelMeta *> &metas);

        std::expected<std::unique_ptr<Session>, Error> Begin();
        Error Commit();
        Error Rollback();
        bool IsTransaction() const;

        const std::string &getConnectionName() const;
        cpporm_sqldriver::SqlDatabase &getDbHandle();
        const cpporm_sqldriver::SqlDatabase &getDbHandle() const;  // Added const overload

        const cpporm::OnConflictClause *getTempOnConflictClause() const;
        void clearTempOnConflictClause();

        static std::string getSqlTypeForCppType(const FieldMeta &field_meta, const QString &driverName_std);
        static void qvariantToAny(const QVariant &qv, const std::type_index &target_cpp_type, std::any &out_any, bool &out_conversion_ok);
        static QueryValue anyToQueryValueForSessionConvenience(const std::any &val);

        static cpporm_sqldriver::SqlValue queryValueToSqlValue(const QueryValue &qv);
        static QueryValue sqlValueToQueryValue(const cpporm_sqldriver::SqlValue &sv);

        // Made public static for access from internal::execute_ddl_query
        static std::pair<cpporm_sqldriver::SqlQuery, Error> execute_query_internal(cpporm_sqldriver::SqlDatabase &db_conn_ref, const std::string &sql, const std::vector<cpporm_sqldriver::SqlValue> &bound_params);

      private:
        Error mapRowToModel(cpporm_sqldriver::SqlQuery &query, ModelBase &model, const ModelMeta &meta);
        internal::SessionModelDataForWrite extractModelData(const ModelBase &model_instance, const ModelMeta &meta, bool for_update = false, bool include_timestamps_even_if_null = false);

        friend class internal_batch_helpers::FriendAccess;

        void autoSetTimestamps(ModelBase &model_instance, const ModelMeta &meta, bool is_create_op);

        Error processPreloadsInternal(const QueryBuilder &qb, std::vector<ModelBase *> &models_raw_ptr);
        Error processPreloads(const QueryBuilder &qb, std::vector<std::unique_ptr<ModelBase>> &loaded_models);
        Error executePreloadForAssociation(const AssociationMeta &assoc_meta, const ModelMeta &parent_model_meta, std::vector<ModelBase *> &parent_models_raw_ptr, const std::string &remaining_nested_preload_path = "");

        std::string connection_name_;
        cpporm_sqldriver::SqlDatabase db_handle_;
        bool is_explicit_transaction_handle_;
        std::unique_ptr<OnConflictClause> temp_on_conflict_clause_;
    };

}  // namespace cpporm

#endif