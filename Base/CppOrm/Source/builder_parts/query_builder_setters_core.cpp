#include "cpporm/builder_parts/query_builder_conditions_mixin.h"
#include "cpporm/model_base.h"
#include "cpporm/query_builder_core.h"
#include <QDebug>

namespace cpporm {

// --- 核心 Model/Table/From 设置器 ---
QueryBuilder &QueryBuilder::Model(const ModelBase *model_instance_hint) {
  if (model_instance_hint) {
    const ModelMeta &meta = model_instance_hint->_getOwnModelMeta();
    this->state_.model_meta_ = &meta;
    if (!meta.table_name.empty()) {
      this->state_.from_clause_source_ = meta.table_name;
    } else {
      this->state_.from_clause_source_ = std::string("");
    }
  } else {
    this->state_.model_meta_ = nullptr;
    this->state_.from_clause_source_ = std::string("");
  }
  return *this;
}

QueryBuilder &QueryBuilder::Model(const ModelMeta &meta) {
  this->state_.model_meta_ = &meta;
  if (!meta.table_name.empty()) {
    this->state_.from_clause_source_ = meta.table_name;
  } else {
    this->state_.from_clause_source_ = std::string("");
  }
  return *this;
}

QueryBuilder &QueryBuilder::Table(std::string table_name) {
  this->state_.from_clause_source_ = std::move(table_name);
  if (this->state_.model_meta_ &&
      this->state_.model_meta_->table_name !=
          std::get<std::string>(this->state_.from_clause_source_)) {
    this->state_.model_meta_ = nullptr;
  }
  return *this;
}

QueryBuilder &QueryBuilder::From(std::string source_name_or_cte_alias) {
  std::string old_from_source_string_if_any;
  if (std::holds_alternative<std::string>(this->state_.from_clause_source_)) {
    old_from_source_string_if_any =
        std::get<std::string>(this->state_.from_clause_source_);
  }

  this->state_.from_clause_source_ = std::move(source_name_or_cte_alias);

  if (this->state_.model_meta_) {
    const std::string &new_from_str =
        std::get<std::string>(this->state_.from_clause_source_);
    bool is_known_cte = false;
    for (const auto &cte_def : this->state_.ctes_) {
      if (cte_def.name == new_from_str) {
        is_known_cte = true;
        break;
      }
    }
    if (is_known_cte || this->state_.model_meta_->table_name != new_from_str) {
      this->state_.model_meta_ = nullptr;
    }
  }
  return *this;
}

QueryBuilder &QueryBuilder::From(const QueryBuilder &subquery_builder,
                                 const std::string &alias) {
  auto sub_expr_expected = subquery_builder.AsSubquery();
  if (!sub_expr_expected.has_value()) {
    qWarning() << "cpporm QueryBuilder::From(QueryBuilder): Failed to create "
                  "subquery expression: "
               << QString::fromStdString(sub_expr_expected.error().message);
    return *this;
  }
  this->state_.from_clause_source_ =
      SubquerySource{std::move(sub_expr_expected.value()), alias};
  this->state_.model_meta_ = nullptr;
  return *this;
}

QueryBuilder &QueryBuilder::From(const SubqueryExpression &subquery_expr,
                                 const std::string &alias) {
  this->state_.from_clause_source_ = SubquerySource{subquery_expr, alias};
  this->state_.model_meta_ = nullptr;
  return *this;
}

// --- Specific Setters ---
QueryBuilder &QueryBuilder::SelectSubquery(const QueryBuilder &subquery_builder,
                                           const std::string &alias) {
  auto sub_expr_expected = subquery_builder.AsSubquery();
  if (!sub_expr_expected.has_value()) {
    qWarning() << "cpporm QueryBuilder::SelectSubquery(QueryBuilder): Failed "
                  "to create subquery expression: "
               << QString::fromStdString(sub_expr_expected.error().message);
    return *this;
  }
  // 使用 AddSelect 将子查询添加到选择列表，它会处理默认的 "*"
  this->QueryBuilderClausesMixin<QueryBuilder>::AddSelect(
      NamedSubqueryField{std::move(sub_expr_expected.value()), alias});
  return *this;
}

QueryBuilder &
QueryBuilder::SelectSubquery(const SubqueryExpression &subquery_expr,
                             const std::string &alias) {
  // 使用 AddSelect 将子查询添加到选择列表
  this->QueryBuilderClausesMixin<QueryBuilder>::AddSelect(
      NamedSubqueryField{subquery_expr, alias});
  return *this;
}

QueryBuilder &QueryBuilder::With(const std::string &cte_name,
                                 const QueryBuilder &cte_query_builder,
                                 bool recursive) {
  auto sub_expr_expected = cte_query_builder.AsSubquery();
  if (!sub_expr_expected.has_value()) {
    qWarning()
        << "cpporm QueryBuilder::With: Failed to create subquery for CTE"
        << QString::fromStdString(cte_name) << ":"
        << QString::fromStdString(sub_expr_expected.error().message);
    return *this;
  }
  state_.ctes_.emplace_back(cte_name, std::move(sub_expr_expected.value()),
                            recursive);
  return *this;
}

QueryBuilder &QueryBuilder::WithRaw(const std::string &cte_name,
                                    const std::string &raw_sql,
                                    const std::vector<QueryValue> &bindings,
                                    bool recursive) {
  std::vector<QueryValueVariantForSubquery> native_bindings;
  native_bindings.reserve(bindings.size());
  for (const auto &qv_arg : bindings) {
    std::visit(
        [&native_bindings](auto &&arg_val) {
          using ArgT = std::decay_t<decltype(arg_val)>;
          if constexpr (std::is_same_v<ArgT, SubqueryExpression>) {
            qWarning()
                << "cpporm QueryBuilder::WithRaw: SubqueryExpression as a "
                   "binding for raw CTE is complex. Only its bindings are "
                   "used.";
            for (const auto &sub_binding : arg_val.bindings) {
              native_bindings.push_back(sub_binding);
            }
          } else if constexpr (std::is_same_v<ArgT, std::nullptr_t> ||
                               std::is_same_v<ArgT, int> ||
                               std::is_same_v<ArgT, long long> ||
                               std::is_same_v<ArgT, double> ||
                               std::is_same_v<ArgT, std::string> ||
                               std::is_same_v<ArgT, bool> ||
                               std::is_same_v<ArgT, QDateTime> ||
                               std::is_same_v<ArgT, QDate> ||
                               std::is_same_v<ArgT, QTime> ||
                               std::is_same_v<ArgT, QByteArray>) {
            native_bindings.push_back(arg_val);
          } else {
            qWarning() << "QueryBuilder::WithRaw: Skipping unsupported "
                          "QueryValue variant type "
                       << typeid(ArgT).name() << " for raw CTE binding.";
          }
        },
        qv_arg);
  }
  state_.ctes_.emplace_back(
      cte_name, SubqueryExpression(raw_sql, native_bindings), recursive);
  return *this;
}

QueryBuilder &QueryBuilder::OnConflictUpdateAllExcluded() {
  if (!state_.on_conflict_clause_) {
    state_.on_conflict_clause_ = std::make_unique<OnConflictClause>();
  }
  state_.on_conflict_clause_->action =
      OnConflictClause::Action::UpdateAllExcluded;
  state_.on_conflict_clause_->update_assignments.clear();
  return *this;
}

QueryBuilder &QueryBuilder::OnConflictDoNothing() {
  if (!state_.on_conflict_clause_) {
    state_.on_conflict_clause_ = std::make_unique<OnConflictClause>();
  }
  state_.on_conflict_clause_->action = OnConflictClause::Action::DoNothing;
  state_.on_conflict_clause_->update_assignments.clear();
  return *this;
}

QueryBuilder &QueryBuilder::OnConflictUpdateSpecific(
    std::function<void(OnConflictUpdateSetter &)> updater_fn) {
  if (!state_.on_conflict_clause_) {
    state_.on_conflict_clause_ = std::make_unique<OnConflictClause>();
  }
  state_.on_conflict_clause_->action = OnConflictClause::Action::UpdateSpecific;
  OnConflictUpdateSetter setter(*state_.on_conflict_clause_);
  updater_fn(setter);
  return *this;
}

// --- Implementations for QueryBuilder's own Where/Or/Not overloads ---

QueryBuilder &QueryBuilder::Where(const QueryBuilder &sub_qb_condition) {
  bool same_table_and_simple_source = false;

  QString this_from_name_qstr = this->getFromSourceName();
  QString sub_from_name_qstr = sub_qb_condition.getFromSourceName();

  if (!this_from_name_qstr.isEmpty() && !sub_from_name_qstr.isEmpty() &&
      this_from_name_qstr == sub_from_name_qstr) {
    if (std::holds_alternative<std::string>(this->state_.from_clause_source_) &&
        std::get<std::string>(this->state_.from_clause_source_) ==
            this_from_name_qstr.toStdString() &&
        std::holds_alternative<std::string>(
            sub_qb_condition.state_.from_clause_source_) &&
        std::get<std::string>(sub_qb_condition.state_.from_clause_source_) ==
            sub_from_name_qstr.toStdString()) {
      same_table_and_simple_source = true;
    } else if (this->state_.model_meta_ &&
               sub_qb_condition.state_.model_meta_ &&
               this->state_.model_meta_ ==
                   sub_qb_condition.state_.model_meta_ &&
               std::holds_alternative<std::string>(
                   this->state_.from_clause_source_) &&
               std::get<std::string>(this->state_.from_clause_source_)
                   .empty() &&
               std::holds_alternative<std::string>(
                   sub_qb_condition.state_.from_clause_source_) &&
               std::get<std::string>(
                   sub_qb_condition.state_.from_clause_source_)
                   .empty()) {
      same_table_and_simple_source = true;
    }
  }

  if (same_table_and_simple_source) {
    auto [sub_cond_sql, sub_cond_args] =
        sub_qb_condition.buildConditionClauseGroup();
    if (!sub_cond_sql.empty()) {
      this->QueryBuilderConditionsMixin<QueryBuilder>::Where(sub_cond_sql,
                                                             sub_cond_args);
    }

    if (!sub_qb_condition.state_.apply_soft_delete_scope_) {
      this->state_.apply_soft_delete_scope_ = false;
    }

  } else {

    auto sub_expr_expected = sub_qb_condition.AsSubquery();
    if (!sub_expr_expected) {
      qWarning() << "QueryBuilder::Where(const QueryBuilder& sub_qb): Failed "
                    "to convert subquery for EXISTS: "
                 << QString::fromStdString(sub_expr_expected.error().message);
      return *this;
    }
    return this->QueryBuilderConditionsMixin<QueryBuilder>::Where(
        "EXISTS (?)",
        std::vector<QueryValue>{std::move(sub_expr_expected.value())});
  }
  return *this;
}

QueryBuilder &QueryBuilder::Or(const QueryBuilder &sub_qb_condition) {
  bool same_table_and_simple_source = false;
  QString this_from_name_qstr = this->getFromSourceName();
  QString sub_from_name_qstr = sub_qb_condition.getFromSourceName();

  if (!this_from_name_qstr.isEmpty() && !sub_from_name_qstr.isEmpty() &&
      this_from_name_qstr == sub_from_name_qstr) {
    if (std::holds_alternative<std::string>(this->state_.from_clause_source_) &&
        std::get<std::string>(this->state_.from_clause_source_) ==
            this_from_name_qstr.toStdString() &&
        std::holds_alternative<std::string>(
            sub_qb_condition.state_.from_clause_source_) &&
        std::get<std::string>(sub_qb_condition.state_.from_clause_source_) ==
            sub_from_name_qstr.toStdString()) {
      same_table_and_simple_source = true;
    } else if (this->state_.model_meta_ &&
               sub_qb_condition.state_.model_meta_ &&
               this->state_.model_meta_ ==
                   sub_qb_condition.state_.model_meta_ &&
               std::holds_alternative<std::string>(
                   this->state_.from_clause_source_) &&
               std::get<std::string>(this->state_.from_clause_source_)
                   .empty() &&
               std::holds_alternative<std::string>(
                   sub_qb_condition.state_.from_clause_source_) &&
               std::get<std::string>(
                   sub_qb_condition.state_.from_clause_source_)
                   .empty()) {
      same_table_and_simple_source = true;
    }
  }

  if (same_table_and_simple_source) {
    auto [sub_cond_sql, sub_cond_args] =
        sub_qb_condition.buildConditionClauseGroup();
    if (!sub_cond_sql.empty()) {
      this->QueryBuilderConditionsMixin<QueryBuilder>::Or(sub_cond_sql,
                                                          sub_cond_args);
    }
    if (!sub_qb_condition.state_.apply_soft_delete_scope_) {
      this->state_.apply_soft_delete_scope_ = false;
    }
  } else {
    auto sub_expr_expected = sub_qb_condition.AsSubquery();
    if (!sub_expr_expected) {
      qWarning() << "QueryBuilder::Or(const QueryBuilder& sub_qb): Failed to "
                    "convert subquery for EXISTS: "
                 << QString::fromStdString(sub_expr_expected.error().message);
      return *this;
    }
    return this->QueryBuilderConditionsMixin<QueryBuilder>::Or(
        "EXISTS (?)",
        std::vector<QueryValue>{std::move(sub_expr_expected.value())});
  }
  return *this;
}

QueryBuilder &QueryBuilder::Not(const QueryBuilder &sub_qb_condition) {
  bool same_table_and_simple_source = false;
  QString this_from_name_qstr = this->getFromSourceName();
  QString sub_from_name_qstr = sub_qb_condition.getFromSourceName();

  if (!this_from_name_qstr.isEmpty() && !sub_from_name_qstr.isEmpty() &&
      this_from_name_qstr == sub_from_name_qstr) {
    if (std::holds_alternative<std::string>(this->state_.from_clause_source_) &&
        std::get<std::string>(this->state_.from_clause_source_) ==
            this_from_name_qstr.toStdString() &&
        std::holds_alternative<std::string>(
            sub_qb_condition.state_.from_clause_source_) &&
        std::get<std::string>(sub_qb_condition.state_.from_clause_source_) ==
            sub_from_name_qstr.toStdString()) {
      same_table_and_simple_source = true;
    } else if (this->state_.model_meta_ &&
               sub_qb_condition.state_.model_meta_ &&
               this->state_.model_meta_ ==
                   sub_qb_condition.state_.model_meta_ &&
               std::holds_alternative<std::string>(
                   this->state_.from_clause_source_) &&
               std::get<std::string>(this->state_.from_clause_source_)
                   .empty() &&
               std::holds_alternative<std::string>(
                   sub_qb_condition.state_.from_clause_source_) &&
               std::get<std::string>(
                   sub_qb_condition.state_.from_clause_source_)
                   .empty()) {
      same_table_and_simple_source = true;
    }
  }

  if (same_table_and_simple_source) {
    auto [sub_cond_sql, sub_cond_args] =
        sub_qb_condition.buildConditionClauseGroup();
    if (!sub_cond_sql.empty()) {
      this->QueryBuilderConditionsMixin<QueryBuilder>::Not(sub_cond_sql,
                                                           sub_cond_args);
    }
    if (!sub_qb_condition.state_.apply_soft_delete_scope_) {
      this->state_.apply_soft_delete_scope_ = false;
    }
  } else {
    auto sub_expr_expected = sub_qb_condition.AsSubquery();
    if (!sub_expr_expected) {
      qWarning() << "QueryBuilder::Not(const QueryBuilder& sub_qb): Failed to "
                    "convert subquery for EXISTS: "
                 << QString::fromStdString(sub_expr_expected.error().message);
      return *this;
    }
    return this->QueryBuilderConditionsMixin<QueryBuilder>::Not(
        "EXISTS (?)",
        std::vector<QueryValue>{std::move(sub_expr_expected.value())});
  }
  return *this;
}

QueryBuilder &QueryBuilder::Where(
    const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
  if (sub_expr_expected.has_value()) {
    return this->QueryBuilderConditionsMixin<QueryBuilder>::Where(
        "EXISTS (?)", std::vector<QueryValue>{sub_expr_expected.value()});
  } else {
#ifdef QT_CORE_LIB
    qWarning() << "QueryBuilder::Where(expected<Subquery>): Subquery "
                  "generation failed: "
               << QString::fromStdString(sub_expr_expected.error().message)
               << ". Condition based on this subquery will not be added.";
#endif
  }
  return *this;
}

QueryBuilder &QueryBuilder::Or(
    const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
  if (sub_expr_expected.has_value()) {
    return this->QueryBuilderConditionsMixin<QueryBuilder>::Or(
        "EXISTS (?)", std::vector<QueryValue>{sub_expr_expected.value()});
  } else {
#ifdef QT_CORE_LIB
    qWarning()
        << "QueryBuilder::Or(expected<Subquery>): Subquery generation failed: "
        << QString::fromStdString(sub_expr_expected.error().message)
        << ". Condition based on this subquery will not be added.";
#endif
  }
  return *this;
}

QueryBuilder &QueryBuilder::Not(
    const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
  if (sub_expr_expected.has_value()) {
    return this->QueryBuilderConditionsMixin<QueryBuilder>::Not(
        "EXISTS (?)", std::vector<QueryValue>{sub_expr_expected.value()});
  } else {
#ifdef QT_CORE_LIB
    qWarning()
        << "QueryBuilder::Not(expected<Subquery>): Subquery generation failed: "
        << QString::fromStdString(sub_expr_expected.error().message)
        << ". Condition based on this subquery will not be added.";
#endif
  }
  return *this;
}

} // namespace cpporm