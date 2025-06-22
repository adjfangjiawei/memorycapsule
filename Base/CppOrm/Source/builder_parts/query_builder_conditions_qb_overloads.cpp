// cpporm/builder_parts/query_builder_conditions_qb_overloads.cpp
#include <QDebug>   // For qWarning
#include <QString>  // For getFromSourceName return type

#include "cpporm/builder_parts/query_builder_state.h"  // For SubqueryExpression, Error, QueryValue
#include "cpporm/query_builder_core.h"                 // For QueryBuilder definition and QueryBuilderConditionsMixin

namespace cpporm {

    // --- Implementations for QueryBuilder's own Where/Or/Not overloads ---
    // These handle QueryBuilder instances as conditions.

    QueryBuilder &QueryBuilder::Where(const QueryBuilder &sub_qb_condition) {
        bool same_table_and_simple_source = false;

        QString this_from_name_qstr = this->getFromSourceName();
        QString sub_from_name_qstr = sub_qb_condition.getFromSourceName();

        if (!this_from_name_qstr.isEmpty() && !sub_from_name_qstr.isEmpty() && this_from_name_qstr == sub_from_name_qstr) {
            // Check if both are simple table names or both are same model implied tables
            if (std::holds_alternative<std::string>(this->state_.from_clause_source_) && std::get<std::string>(this->state_.from_clause_source_) == this_from_name_qstr.toStdString() &&                       // Current QB uses this table name directly
                std::holds_alternative<std::string>(sub_qb_condition.state_.from_clause_source_) && std::get<std::string>(sub_qb_condition.state_.from_clause_source_) == sub_from_name_qstr.toStdString()) {  // Sub QB uses this table name directly
                same_table_and_simple_source = true;
            } else if (this->state_.model_meta_ && sub_qb_condition.state_.model_meta_ && this->state_.model_meta_ == sub_qb_condition.state_.model_meta_ &&                           // Same model
                       std::holds_alternative<std::string>(this->state_.from_clause_source_) && std::get<std::string>(this->state_.from_clause_source_).empty() &&                     // Current QB implies model table
                       std::holds_alternative<std::string>(sub_qb_condition.state_.from_clause_source_) && std::get<std::string>(sub_qb_condition.state_.from_clause_source_).empty()  // Sub QB implies model table
            ) {
                same_table_and_simple_source = true;
            }
        }

        if (same_table_and_simple_source) {
            // Merge conditions directly
            auto [sub_cond_sql, sub_cond_args] = sub_qb_condition.buildConditionClauseGroup();
            if (!sub_cond_sql.empty()) {
                this->QueryBuilderConditionsMixin<QueryBuilder>::Where(sub_cond_sql, sub_cond_args);
            }
            // If the sub-condition disables soft delete, propagate that
            if (!sub_qb_condition.state_.apply_soft_delete_scope_) {
                this->state_.apply_soft_delete_scope_ = false;
            }
        } else {
            // Use EXISTS (subquery)
            auto sub_expr_expected = sub_qb_condition.AsSubquery();
            if (!sub_expr_expected) {
                qWarning() << "QueryBuilder::Where(const QueryBuilder& sub_qb): Failed "
                              "to convert subquery for EXISTS: "
                           << QString::fromStdString(sub_expr_expected.error().message);
                return *this;
            }
            return this->QueryBuilderConditionsMixin<QueryBuilder>::Where("EXISTS (?)", std::vector<QueryValue>{std::move(sub_expr_expected.value())});
        }
        return *this;
    }

    QueryBuilder &QueryBuilder::Or(const QueryBuilder &sub_qb_condition) {
        bool same_table_and_simple_source = false;
        QString this_from_name_qstr = this->getFromSourceName();
        QString sub_from_name_qstr = sub_qb_condition.getFromSourceName();

        if (!this_from_name_qstr.isEmpty() && !sub_from_name_qstr.isEmpty() && this_from_name_qstr == sub_from_name_qstr) {
            if (std::holds_alternative<std::string>(this->state_.from_clause_source_) && std::get<std::string>(this->state_.from_clause_source_) == this_from_name_qstr.toStdString() && std::holds_alternative<std::string>(sub_qb_condition.state_.from_clause_source_) &&
                std::get<std::string>(sub_qb_condition.state_.from_clause_source_) == sub_from_name_qstr.toStdString()) {
                same_table_and_simple_source = true;
            } else if (this->state_.model_meta_ && sub_qb_condition.state_.model_meta_ && this->state_.model_meta_ == sub_qb_condition.state_.model_meta_ && std::holds_alternative<std::string>(this->state_.from_clause_source_) && std::get<std::string>(this->state_.from_clause_source_).empty() &&
                       std::holds_alternative<std::string>(sub_qb_condition.state_.from_clause_source_) && std::get<std::string>(sub_qb_condition.state_.from_clause_source_).empty()) {
                same_table_and_simple_source = true;
            }
        }

        if (same_table_and_simple_source) {
            auto [sub_cond_sql, sub_cond_args] = sub_qb_condition.buildConditionClauseGroup();
            if (!sub_cond_sql.empty()) {
                this->QueryBuilderConditionsMixin<QueryBuilder>::Or(sub_cond_sql, sub_cond_args);
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
            return this->QueryBuilderConditionsMixin<QueryBuilder>::Or("EXISTS (?)", std::vector<QueryValue>{std::move(sub_expr_expected.value())});
        }
        return *this;
    }

    QueryBuilder &QueryBuilder::Not(const QueryBuilder &sub_qb_condition) {
        bool same_table_and_simple_source = false;
        QString this_from_name_qstr = this->getFromSourceName();
        QString sub_from_name_qstr = sub_qb_condition.getFromSourceName();

        if (!this_from_name_qstr.isEmpty() && !sub_from_name_qstr.isEmpty() && this_from_name_qstr == sub_from_name_qstr) {
            if (std::holds_alternative<std::string>(this->state_.from_clause_source_) && std::get<std::string>(this->state_.from_clause_source_) == this_from_name_qstr.toStdString() && std::holds_alternative<std::string>(sub_qb_condition.state_.from_clause_source_) &&
                std::get<std::string>(sub_qb_condition.state_.from_clause_source_) == sub_from_name_qstr.toStdString()) {
                same_table_and_simple_source = true;
            } else if (this->state_.model_meta_ && sub_qb_condition.state_.model_meta_ && this->state_.model_meta_ == sub_qb_condition.state_.model_meta_ && std::holds_alternative<std::string>(this->state_.from_clause_source_) && std::get<std::string>(this->state_.from_clause_source_).empty() &&
                       std::holds_alternative<std::string>(sub_qb_condition.state_.from_clause_source_) && std::get<std::string>(sub_qb_condition.state_.from_clause_source_).empty()) {
                same_table_and_simple_source = true;
            }
        }

        if (same_table_and_simple_source) {
            auto [sub_cond_sql, sub_cond_args] = sub_qb_condition.buildConditionClauseGroup();
            if (!sub_cond_sql.empty()) {
                this->QueryBuilderConditionsMixin<QueryBuilder>::Not(sub_cond_sql, sub_cond_args);
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
            return this->QueryBuilderConditionsMixin<QueryBuilder>::Not("EXISTS (?)", std::vector<QueryValue>{std::move(sub_expr_expected.value())});
        }
        return *this;
    }

    // Overloads for std::expected<SubqueryExpression, Error>
    QueryBuilder &QueryBuilder::Where(const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
        if (sub_expr_expected.has_value()) {
            return this->QueryBuilderConditionsMixin<QueryBuilder>::Where("EXISTS (?)", std::vector<QueryValue>{sub_expr_expected.value()});
        } else {
#ifdef QT_CORE_LIB
            qWarning() << "QueryBuilder::Where(expected<Subquery>): Subquery "
                          "generation failed: "
                       << QString::fromStdString(sub_expr_expected.error().message) << ". Condition based on this subquery will not be added.";
#endif
        }
        return *this;
    }

    QueryBuilder &QueryBuilder::Or(const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
        if (sub_expr_expected.has_value()) {
            return this->QueryBuilderConditionsMixin<QueryBuilder>::Or("EXISTS (?)", std::vector<QueryValue>{sub_expr_expected.value()});
        } else {
#ifdef QT_CORE_LIB
            qWarning() << "QueryBuilder::Or(expected<Subquery>): Subquery generation failed: " << QString::fromStdString(sub_expr_expected.error().message) << ". Condition based on this subquery will not be added.";
#endif
        }
        return *this;
    }

    QueryBuilder &QueryBuilder::Not(const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
        if (sub_expr_expected.has_value()) {
            return this->QueryBuilderConditionsMixin<QueryBuilder>::Not("EXISTS (?)", std::vector<QueryValue>{sub_expr_expected.value()});
        } else {
#ifdef QT_CORE_LIB
            qWarning() << "QueryBuilder::Not(expected<Subquery>): Subquery generation failed: " << QString::fromStdString(sub_expr_expected.error().message) << ". Condition based on this subquery will not be added.";
#endif
        }
        return *this;
    }

}  // namespace cpporm