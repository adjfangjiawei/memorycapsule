#ifndef cpporm_QUERY_BUILDER_CORE_H
#define cpporm_QUERY_BUILDER_CORE_H

#include "cpporm/builder_parts/query_builder_state.h"
#include "cpporm/error.h"
#include "cpporm/i_query_executor.h"
#include "cpporm/model_base.h"
#include "cpporm/query_builder_fwd.h"

#include "cpporm/builder_parts/query_builder_clauses_mixin.h"
#include "cpporm/builder_parts/query_builder_conditions_mixin.h"
#include "cpporm/builder_parts/query_builder_joins_mixin.h"
#include "cpporm/builder_parts/query_builder_preload_mixin.h"
#include "cpporm/builder_parts/query_builder_scopes_mixin.h"

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace cpporm {

class OnConflictUpdateSetter {
public:
  explicit OnConflictUpdateSetter(OnConflictClause &clause_ref);
  OnConflictUpdateSetter &Set(const std::string &db_column_name,
                              const QueryValue &value);
  OnConflictUpdateSetter &
  Set(const std::map<std::string, QueryValue> &assignments);

private:
  OnConflictClause &clause_;
};

class QueryBuilder : public QueryBuilderConditionsMixin<QueryBuilder>,
                     public QueryBuilderClausesMixin<QueryBuilder>,
                     public QueryBuilderJoinsMixin<QueryBuilder>,
                     public QueryBuilderScopesMixin<QueryBuilder>,
                     public QueryBuilderPreloadMixin<QueryBuilder> {
public:
  // Bring Where, Or, Not, In methods from QueryBuilderConditionsMixin into
  // scope
  using QueryBuilderConditionsMixin<QueryBuilder>::Where;
  using QueryBuilderConditionsMixin<QueryBuilder>::Or;
  using QueryBuilderConditionsMixin<QueryBuilder>::Not;
  using QueryBuilderConditionsMixin<QueryBuilder>::In; // Added In

  // --- 构造函数和析构函数 ---
  explicit QueryBuilder(IQueryExecutor *executor, QString connection_name,
                        const ModelMeta *model_meta = nullptr);
  QueryBuilder(const QueryBuilder &other);
  QueryBuilder &operator=(const QueryBuilder &other);
  QueryBuilder(QueryBuilder &&other) noexcept;
  QueryBuilder &operator=(QueryBuilder &&other) noexcept;
  ~QueryBuilder();

  // --- 核心 Model/Table/From 设置器 ---
  QueryBuilder &Model(const ModelBase *model_instance_hint);
  QueryBuilder &Model(const ModelMeta &meta);
  template <typename T> QueryBuilder &Model() {
    static_assert(std::is_base_of<ModelBase, T>::value,
                  "T must be a descendant of cpporm::ModelBase");
    return this->Model(T::getModelMeta());
  }
  QueryBuilder &Table(std::string table_name);

  QueryBuilder &From(const QueryBuilder &subquery_builder,
                     const std::string &alias);
  QueryBuilder &From(const SubqueryExpression &subquery_expr,
                     const std::string &alias);
  QueryBuilder &From(std::string source_name_or_cte_alias);

  // --- Specific Setters ---
  QueryBuilder &OnConflictUpdateAllExcluded();
  QueryBuilder &OnConflictDoNothing();
  QueryBuilder &OnConflictUpdateSpecific(
      std::function<void(OnConflictUpdateSetter &)> updater_fn);

  QueryBuilder &With(const std::string &cte_name,
                     const QueryBuilder &cte_query_builder,
                     bool recursive = false);
  QueryBuilder &WithRaw(const std::string &cte_name, const std::string &raw_sql,
                        const std::vector<QueryValue> &bindings = {},
                        bool recursive = false);

  QueryBuilder &SelectSubquery(const QueryBuilder &subquery_builder,
                               const std::string &alias);
  QueryBuilder &SelectSubquery(const SubqueryExpression &subquery_expr,
                               const std::string &alias);

  // --- QueryBuilder specific Where/Or/Not overloads ---
  QueryBuilder &Where(const QueryBuilder &sub_qb_condition);
  QueryBuilder &Or(const QueryBuilder &sub_qb_condition);
  QueryBuilder &Not(const QueryBuilder &sub_qb_condition);

  // New overloads for std::expected<SubqueryExpression, Error>
  QueryBuilder &
  Where(const std::expected<SubqueryExpression, Error> &sub_expr_expected);
  QueryBuilder &
  Or(const std::expected<SubqueryExpression, Error> &sub_expr_expected);
  QueryBuilder &
  Not(const std::expected<SubqueryExpression, Error> &sub_expr_expected);

  // --- SQL 构建和转换方法 ---
  std::pair<QString, QVariantList>
  buildSelectSQL(bool for_subquery_generation = false) const;
  std::pair<QString, QVariantList>
  buildInsertSQLSuffix(const std::vector<std::string>
                           &inserted_columns_db_names_for_values_clause) const;
  std::pair<QString, QVariantList>
  buildUpdateSQL(const std::map<std::string, QueryValue> &updates) const;
  std::pair<QString, QVariantList> buildDeleteSQL() const;
  std::expected<SubqueryExpression, Error> AsSubquery() const;
  std::pair<std::string, std::vector<QueryValue>>
  buildConditionClauseGroup() const;

  // --- 状态访问器 ---
  const ModelMeta *getModelMeta() const { return state_.model_meta_; }
  QString getFromSourceName() const;
  const FromClauseSource &getFromClauseSource() const {
    return state_.from_clause_source_;
  }
  const QString &getConnectionName() const { return connection_name_; }
  IQueryExecutor *getExecutor() const { return executor_; }

  const std::vector<Condition> &getWhereConditions() const {
    return QueryBuilderConditionsMixin<
        QueryBuilder>::getWhereConditions_mixin();
  }
  const std::vector<Condition> &getOrConditions() const {
    return QueryBuilderConditionsMixin<QueryBuilder>::getOrConditions_mixin();
  }
  const std::vector<Condition> &getNotConditions() const {
    return QueryBuilderConditionsMixin<QueryBuilder>::getNotConditions_mixin();
  }
  const std::vector<CTEState> &getCTEs() const { return state_.ctes_; }
  const std::vector<SelectField> &getSelectFields() const {
    return state_.select_fields_;
  }
  const std::string &getOrderClause() const {
    return QueryBuilderClausesMixin<QueryBuilder>::getOrderClause_mixin();
  }
  int getLimitVal() const {
    return QueryBuilderClausesMixin<QueryBuilder>::getLimitVal_mixin();
  }
  int getOffsetVal() const {
    return QueryBuilderClausesMixin<QueryBuilder>::getOffsetVal_mixin();
  }
  const std::string &getGroupClause() const {
    return QueryBuilderClausesMixin<QueryBuilder>::getGroupClause_mixin();
  }
  const Condition *getHavingCondition() const {
    return QueryBuilderClausesMixin<QueryBuilder>::getHavingCondition_mixin();
  }
  const std::vector<JoinClause> &getJoinClauses() const {
    return QueryBuilderJoinsMixin<QueryBuilder>::getJoinClauses_mixin();
  }
  bool isSoftDeleteScopeActive() const {
    return QueryBuilderScopesMixin<
        QueryBuilder>::isSoftDeleteScopeActive_mixin();
  }
  const std::vector<PreloadRequest> &getPreloadRequests() const {
    return QueryBuilderPreloadMixin<QueryBuilder>::getPreloadRequests_mixin();
  }
  const OnConflictClause *getOnConflictClause() const {
    return state_.on_conflict_clause_.get();
  }

  // --- 公共静态方法 ---
  static QVariant toQVariant(const QueryValue &qv,
                             QVariantList &subquery_bindings_accumulator);
  static QueryValue qvariantToQueryValue(const QVariant &qv);
  static std::string quoteSqlIdentifier(const std::string &identifier);

  // --- 内部状态访问 (主要供 Mixin 使用) ---
  QueryBuilderState &getState_() { return state_; }
  const QueryBuilderState &getState_() const { return state_; }

  // --- 调试方法 ---
  QString toSqlDebug() const;

  // --- 模板化执行方法 (声明) ---
  template <typename T> Error First(T *result_model);
  template <typename T>
  Error First(T *result_model, const QueryValue &primary_key_value);
  template <typename T>
  Error First(T *result_model,
              const std::vector<QueryValue> &primary_key_values);
  template <typename T>
  Error First(T *result_model,
              const std::map<std::string, QueryValue> &conditions);

  template <typename T> Error Find(std::vector<T> *results_vector);
  template <typename T>
  Error Find(std::vector<T> *results_vector,
             const std::map<std::string, QueryValue> &conditions);
  template <typename T>
  Error Find(std::vector<T> *results_vector, const std::string &query_string,
             const std::vector<QueryValue> &args = {});

  template <typename T>
  Error Find(std::vector<std::unique_ptr<T>> *results_vector);
  template <typename T>
  Error Find(std::vector<std::unique_ptr<T>> *results_vector,
             const std::map<std::string, QueryValue> &conditions);
  template <typename T>
  Error Find(std::vector<std::unique_ptr<T>> *results_vector,
             const std::string &query_string,
             const std::vector<QueryValue> &args = {});

  template <typename TModel>
  std::expected<QVariant, Error> Create(TModel &model);

  template <typename TModel>
  std::expected<long long, Error> Save(TModel &model);

  // --- 非模板化执行方法 (声明) ---
  Error First(ModelBase &result_model);
  Error Find(std::vector<std::unique_ptr<ModelBase>> &results_vector,
             std::function<std::unique_ptr<ModelBase>()> element_type_factory);
  std::expected<QVariant, Error>
  Create(ModelBase &model,
         const OnConflictClause *conflict_options_override = nullptr);
  std::expected<long long, Error>
  Updates(const std::map<std::string, QueryValue> &updates);
  std::expected<long long, Error> Delete();
  std::expected<long long, Error> Save(ModelBase &model);
  std::expected<int64_t, Error> Count();

private:
  friend class QueryBuilderConditionsMixin<QueryBuilder>;
  friend class QueryBuilderClausesMixin<QueryBuilder>;
  friend class QueryBuilderJoinsMixin<QueryBuilder>;
  friend class QueryBuilderScopesMixin<QueryBuilder>;
  friend class QueryBuilderPreloadMixin<QueryBuilder>;

  IQueryExecutor *executor_;
  QString connection_name_;
  QueryBuilderState state_;

  void build_condition_logic_internal(
      std::ostringstream &sql_stream, QVariantList &bound_params_accumulator,
      bool &first_overall_condition_written,
      const std::string &prepended_scope_sql = "") const;
  void build_ctes_sql_prefix(std::ostringstream &sql_stream,
                             QVariantList &bound_params_accumulator) const;

  // Declaration of the static helper for building condition blocks
  static bool build_one_condition_block_internal_static_helper(
      std::ostringstream &to_stream, QVariantList &bindings_acc,
      const std::vector<Condition> &conditions_group,
      const std::string &op_within_group, bool is_not_group);
};

inline OnConflictUpdateSetter::OnConflictUpdateSetter(
    OnConflictClause &clause_ref)
    : clause_(clause_ref) {
  clause_.action = OnConflictClause::Action::UpdateSpecific;
}
inline OnConflictUpdateSetter &
OnConflictUpdateSetter::Set(const std::string &db_column_name,
                            const QueryValue &value) {
  clause_.update_assignments[db_column_name] = value;
  return *this;
}
inline OnConflictUpdateSetter &OnConflictUpdateSetter::Set(
    const std::map<std::string, QueryValue> &assignments) {
  for (const auto &pair : assignments) {
    clause_.update_assignments[pair.first] = pair.second;
  }
  return *this;
}

} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_CORE_H