// cpporm/builder_parts/query_builder_helpers.cpp
#include <QDebug>
#include <QMetaType>  // For QMetaType::UnknownType
#include <QVariant>
#include <sstream>
#include <variant>  // For std::visit

#include "cpporm/model_base.h"     // For ModelMeta related logic
#include "cpporm/query_builder.h"  // For QueryBuilder class and its members like quoteSqlIdentifier, toQVariant

namespace cpporm {

    void QueryBuilder::build_ctes_sql_prefix(std::ostringstream &sql_stream, QVariantList &bound_params_accumulator) const {
        if (state_.ctes_.empty()) {
            return;
        }
        sql_stream << "WITH ";
        bool has_recursive_cte = false;
        for (const auto &cte_state : state_.ctes_) {
            if (cte_state.recursive) {
                has_recursive_cte = true;
                break;
            }
        }
        if (has_recursive_cte) {
            sql_stream << "RECURSIVE ";
        }
        for (size_t i = 0; i < state_.ctes_.size(); ++i) {
            const auto &cte = state_.ctes_[i];
            sql_stream << quoteSqlIdentifier(cte.name) << " AS (";
            sql_stream << cte.query.sql_string;
            sql_stream << ")";
            for (const auto &binding_variant : cte.query.bindings) {
                std::visit(
                    [&bound_params_accumulator](auto &&arg_val) {
                        using ArgT = std::decay_t<decltype(arg_val)>;
                        if constexpr (std::is_same_v<ArgT, std::nullptr_t>) {
                            bound_params_accumulator.append(QVariant(QMetaType(QMetaType::UnknownType)));
                        } else if constexpr (std::is_same_v<ArgT, std::string>) {
                            bound_params_accumulator.append(QString::fromStdString(arg_val));
                        } else if constexpr (  // Using QVariant::fromValue for Qt types directly
                            std::is_same_v<ArgT, QDateTime> || std::is_same_v<ArgT, QDate> || std::is_same_v<ArgT, QTime> || std::is_same_v<ArgT, QByteArray> || std::is_same_v<ArgT, bool> || std::is_same_v<ArgT, int> || std::is_same_v<ArgT, long long> || std::is_same_v<ArgT, double>) {
                            bound_params_accumulator.append(QVariant::fromValue(arg_val));
                        } else {
                            qWarning() << "QueryBuilder::build_ctes_sql_prefix: Unhandled "
                                          "native type in CTE binding for QVariant conversion:"
                                       << typeid(ArgT).name();
                        }
                    },
                    binding_variant);
            }
            if (i < state_.ctes_.size() - 1) {
                sql_stream << ", ";
            }
        }
        sql_stream << " ";
    }

    bool QueryBuilder::build_one_condition_block_internal_static_helper(std::ostringstream &to_stream, QVariantList &bindings_acc, const std::vector<Condition> &conditions_group, const std::string &op_within_group, bool is_not_group) {
        if (conditions_group.empty()) {
            return false;
        }
        if (is_not_group) {
            to_stream << "NOT ";
        }
        to_stream << "(";
        for (size_t i = 0; i < conditions_group.size(); ++i) {
            if (i > 0) {
                to_stream << " " << op_within_group << " ";
            }
            const std::string &local_query_string = conditions_group[i].query_string;
            const std::vector<QueryValue> &local_args = conditions_group[i].args;
            std::string::size_type last_pos = 0;
            std::string::size_type find_pos = 0;
            int arg_idx = 0;
            while ((find_pos = local_query_string.find('?', last_pos)) != std::string::npos) {
                to_stream << local_query_string.substr(last_pos, find_pos - last_pos);
                if (arg_idx < static_cast<int>(local_args.size())) {
                    const auto &arg_value = local_args[arg_idx++];
                    QVariant qv_arg = QueryBuilder::toQVariant(arg_value, bindings_acc);  // bindings_acc is passed by ref
                    if (std::holds_alternative<SubqueryExpression>(arg_value)) {
                        to_stream << qv_arg.toString().toStdString();
                    } else {
                        to_stream << "?";
                        // bindings_acc.append(qv_arg); // This is now done inside toQVariant for non-subquery values
                        // or if toQVariant is changed to not append, it should be here.
                        // Given `toQVariant` appends subquery bindings,
                        // for non-subquery, `toQVariant` should just return the QVariant,
                        // and `bindings_acc.append(qv_arg)` should happen here.
                        // Let's assume `toQVariant` only appends for subqueries.
                        if (!std::holds_alternative<SubqueryExpression>(arg_value)) {  // Append only if not subquery
                            bindings_acc.append(qv_arg);
                        }
                    }
                } else {
                    qWarning() << "cpporm: Not enough arguments for placeholders in condition string:" << QString::fromStdString(local_query_string);
                    to_stream << "?";
                }
                last_pos = find_pos + 1;
            }
            to_stream << local_query_string.substr(last_pos);
            if (arg_idx < static_cast<int>(local_args.size())) {
                bool only_subqueries_left = true;
                for (size_t k = arg_idx; k < local_args.size(); ++k) {
                    if (!std::holds_alternative<SubqueryExpression>(local_args[k])) {
                        only_subqueries_left = false;
                        break;
                    }
                }
                if (!only_subqueries_left) {
                    qWarning() << "cpporm: Too many non-subquery arguments for placeholders in condition string:" << QString::fromStdString(local_query_string);
                }
            }
        }
        to_stream << ")";
        return true;
    }

    // In build_condition_logic_internal, the variable main_clause_keyword_written_this_call
    // was indeed assigned but its value was not subsequently used to make a decision.
    // The logic relied more on first_overall_condition_written and prepended_scope_sql.empty().
    // I'll simplify this part.
    void QueryBuilder::build_condition_logic_internal(std::ostringstream &sql_stream, QVariantList &bound_params_accumulator, bool &first_overall_condition_written, const std::string &prepended_scope_sql) const {
        std::ostringstream user_conditions_builder_ss;
        QVariantList user_conditions_bindings_list;
        bool any_user_condition_written_in_this_block = false;

        if (!state_.where_conditions_.empty()) {
            any_user_condition_written_in_this_block = build_one_condition_block_internal_static_helper(user_conditions_builder_ss, user_conditions_bindings_list, state_.where_conditions_, "AND", false) || any_user_condition_written_in_this_block;
        }
        if (!state_.or_conditions_.empty()) {
            if (any_user_condition_written_in_this_block) user_conditions_builder_ss << " OR ";
            any_user_condition_written_in_this_block = build_one_condition_block_internal_static_helper(user_conditions_builder_ss, user_conditions_bindings_list, state_.or_conditions_, "OR", false) || any_user_condition_written_in_this_block;
        }
        if (!state_.not_conditions_.empty()) {
            if (any_user_condition_written_in_this_block) user_conditions_builder_ss << " AND ";
            any_user_condition_written_in_this_block = build_one_condition_block_internal_static_helper(user_conditions_builder_ss, user_conditions_bindings_list, state_.not_conditions_, "AND", true) || any_user_condition_written_in_this_block;
        }

        std::string user_conditions_final_sql = user_conditions_builder_ss.str();
        bool wrote_something_in_this_call = false;

        if (!prepended_scope_sql.empty()) {
            if (first_overall_condition_written) {
                sql_stream << " WHERE ";
                first_overall_condition_written = false;
            } else {
                // Check if sql_stream is not empty and doesn't end with an opening parenthesis or operator
                std::string current_sql_in_stream_str = sql_stream.str();
                if (!current_sql_in_stream_str.empty()) {
                    char last_char = ' ';
                    for (auto it = current_sql_in_stream_str.rbegin(); it != current_sql_in_stream_str.rend(); ++it) {
                        if (*it != ' ') {
                            last_char = *it;
                            break;
                        }
                    }
                    if (last_char != '(') sql_stream << " AND ";
                }
            }
            sql_stream << "(" << prepended_scope_sql << ")";
            wrote_something_in_this_call = true;
        }

        if (!user_conditions_final_sql.empty()) {
            if (first_overall_condition_written) {
                sql_stream << " WHERE ";
                first_overall_condition_written = false;
            } else {
                std::string current_sql_in_stream_str = sql_stream.str();
                if (!current_sql_in_stream_str.empty()) {  // If something was already written (e.g. scope)
                    char last_char = ' ';
                    for (auto it = current_sql_in_stream_str.rbegin(); it != current_sql_in_stream_str.rend(); ++it) {
                        if (*it != ' ') {
                            last_char = *it;
                            break;
                        }
                    }
                    if (last_char != '(') sql_stream << " AND ";
                }
            }
            sql_stream << "(" << user_conditions_final_sql << ")";
            bound_params_accumulator.append(user_conditions_bindings_list);
            // wrote_something_in_this_call = true; // Not strictly needed to track for this variable's original warning.
        }
    }

}  // namespace cpporm