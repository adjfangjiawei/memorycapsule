// cpporm/builder_parts/query_builder_cte_select_subquery.cpp
#include <QDebug>   // For qWarning
#include <variant>  // For std::visit in WithRaw

#include "cpporm/builder_parts/query_builder_state.h"  // For SubqueryExpression, QueryValue, CTEState, NamedSubqueryField
#include "cpporm/query_builder_core.h"                 // For QueryBuilder definition and QueryBuilderClausesMixin

namespace cpporm {

    QueryBuilder &QueryBuilder::SelectSubquery(const QueryBuilder &subquery_builder, const std::string &alias) {
        auto sub_expr_expected = subquery_builder.AsSubquery();
        if (!sub_expr_expected.has_value()) {
            qWarning() << "cpporm QueryBuilder::SelectSubquery(QueryBuilder): Failed "
                          "to create subquery expression: "
                       << QString::fromStdString(sub_expr_expected.error().message);
            return *this;
        }
        // AddSelect is part of QueryBuilderClausesMixin
        this->AddSelect(NamedSubqueryField{std::move(sub_expr_expected.value()), alias});
        return *this;
    }

    QueryBuilder &QueryBuilder::SelectSubquery(const SubqueryExpression &subquery_expr, const std::string &alias) {
        this->AddSelect(NamedSubqueryField{subquery_expr, alias});
        return *this;
    }

    QueryBuilder &QueryBuilder::With(const std::string &cte_name, const QueryBuilder &cte_query_builder, bool recursive) {
        auto sub_expr_expected = cte_query_builder.AsSubquery();
        if (!sub_expr_expected.has_value()) {
            qWarning() << "cpporm QueryBuilder::With: Failed to create subquery for CTE '" << QString::fromStdString(cte_name) << "': " << QString::fromStdString(sub_expr_expected.error().message);
            return *this;
        }
        state_.ctes_.emplace_back(cte_name, std::move(sub_expr_expected.value()), recursive);
        return *this;
    }

    QueryBuilder &QueryBuilder::WithRaw(const std::string &cte_name, const std::string &raw_sql, const std::vector<QueryValue> &bindings, bool recursive) {
        std::vector<QueryValueVariantForSubquery> native_bindings;
        native_bindings.reserve(bindings.size());
        for (const auto &qv_arg : bindings) {
            std::visit(
                [&native_bindings](auto &&arg_val) {
                    using ArgT = std::decay_t<decltype(arg_val)>;
                    if constexpr (std::is_same_v<ArgT, SubqueryExpression>) {
                        qWarning() << "cpporm QueryBuilder::WithRaw: SubqueryExpression as a "
                                      "binding for raw CTE is complex. Only its bindings are "
                                      "used.";
                        for (const auto &sub_binding : arg_val.bindings) {
                            native_bindings.push_back(sub_binding);
                        }
                    } else if constexpr (std::is_same_v<ArgT, std::nullptr_t> || std::is_same_v<ArgT, int> || std::is_same_v<ArgT, long long> || std::is_same_v<ArgT, double> || std::is_same_v<ArgT, std::string> || std::is_same_v<ArgT, bool> || std::is_same_v<ArgT, QDateTime> ||
                                         std::is_same_v<ArgT, QDate> || std::is_same_v<ArgT, QTime> || std::is_same_v<ArgT, QByteArray>) {
                        native_bindings.push_back(arg_val);
                    } else {
                        qWarning() << "QueryBuilder::WithRaw: Skipping unsupported "
                                      "QueryValue variant type '"
                                   << typeid(ArgT).name() << "' for raw CTE binding.";
                    }
                },
                qv_arg);
        }
        state_.ctes_.emplace_back(cte_name, SubqueryExpression(raw_sql, std::move(native_bindings)), recursive);
        return *this;
    }

}  // namespace cpporm