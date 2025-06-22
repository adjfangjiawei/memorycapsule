// cpporm/builder_parts/query_builder_helpers.cpp
#include <QDebug>
#include <QMetaType>  // For QMetaType::UnknownType
#include <QVariant>
#include <sstream>
#include <variant>  // For std::visit

#include "cpporm/model_base.h"     // For ModelMeta related logic
#include "cpporm/query_builder.h"  // For QueryBuilder class and its members like quoteSqlIdentifier, toQVariant

namespace cpporm {

    // --- Private SQL Building Helper Implementations for QueryBuilder ---

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
            sql_stream << quoteSqlIdentifier(cte.name) << " AS (";  // Static call is fine
            sql_stream << cte.query.sql_string;
            sql_stream << ")";

            // Bindings for CTEs are QueryValueVariantForSubquery, convert to QVariant
            for (const auto &binding_variant : cte.query.bindings) {
                std::visit(
                    [&bound_params_accumulator](auto &&arg_val) {
                        using ArgT = std::decay_t<decltype(arg_val)>;
                        if constexpr (std::is_same_v<ArgT, std::nullptr_t>) {
                            bound_params_accumulator.append(QVariant(QMetaType(QMetaType::UnknownType)));
                        } else if constexpr (std::is_same_v<ArgT, std::string>) {
                            bound_params_accumulator.append(QString::fromStdString(arg_val));
                        } else if constexpr (std::is_same_v<ArgT, int> || std::is_same_v<ArgT, long long> || std::is_same_v<ArgT, double> || std::is_same_v<ArgT, bool> || std::is_same_v<ArgT, QDateTime> || std::is_same_v<ArgT, QDate> || std::is_same_v<ArgT, QTime> ||
                                             std::is_same_v<ArgT, QByteArray>) {
                            bound_params_accumulator.append(QVariant::fromValue(arg_val));
                        } else {
                            // This static_assert will fail compilation if a type is not handled,
                            // which is better than a runtime warning for unhandled types in a variant.
                            // static_assert(false, "Unhandled type in QueryValueVariantForSubquery to QVariant conversion for CTE");
                            // Or, keep the qWarning for less strictness during development if new types are added to QueryValueVariantForSubquery
                            qWarning() << "QueryBuilder::build_ctes_sql_prefix: Unhandled "
                                          "native type in "
                                          "CTE binding for QVariant conversion:"
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

    // Static helper, so QueryBuilder::toQVariant must be static or called on an instance
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
                    // QueryBuilder::toQVariant is static, so this is fine.
                    // It appends to bindings_acc if arg_value is not a SubqueryExpression.
                    QVariant qv_arg = QueryBuilder::toQVariant(arg_value, bindings_acc);
                    if (std::holds_alternative<SubqueryExpression>(arg_value)) {
                        // If it's a subquery, its SQL is directly injected.
                        // Its bindings were already added to bindings_acc by toQVariant.
                        to_stream << qv_arg.toString().toStdString();  // qv_arg here holds "(subquery_sql_string)"
                    } else {
                        to_stream << "?";  // Placeholder for regular values
                                           // bindings_acc.append(qv_arg); // This is now done *inside* toQVariant for non-subqueries
                    }
                } else {
                    qWarning() << "cpporm: Not enough arguments for placeholders in "
                                  "condition string:"
                               << QString::fromStdString(local_query_string);
                    to_stream << "?";  // Still output placeholder to maintain query structure
                }
                last_pos = find_pos + 1;
            }
            to_stream << local_query_string.substr(last_pos);

            // Check for too many arguments (excluding SubqueryExpressions that don't consume '?')
            if (arg_idx < static_cast<int>(local_args.size())) {
                bool only_subqueries_left = true;
                for (size_t k = arg_idx; k < local_args.size(); ++k) {
                    if (!std::holds_alternative<SubqueryExpression>(local_args[k])) {
                        only_subqueries_left = false;
                        break;
                    }
                }
                if (!only_subqueries_left) {
                    qWarning() << "cpporm: Too many non-subquery arguments for "
                                  "placeholders in condition string:"
                               << QString::fromStdString(local_query_string);
                }
            }
        }
        to_stream << ")";
        return true;
    }

    void QueryBuilder::build_condition_logic_internal(std::ostringstream &sql_stream,
                                                      QVariantList &bound_params_accumulator,
                                                      bool &first_overall_condition_written,           // True if "WHERE" or "HAVING" needs to be written by this call for the main user conditions
                                                      const std::string &prepended_scope_sql) const {  // e.g., soft delete SQL

        std::ostringstream user_conditions_builder_ss;
        QVariantList user_conditions_bindings_list;  // Bindings specific to this block of user conditions
        bool any_user_condition_written_in_block = false;

        if (!state_.where_conditions_.empty()) {
            // Call static helper
            QueryBuilder::build_one_condition_block_internal_static_helper(user_conditions_builder_ss,
                                                                           user_conditions_bindings_list,  // Pass local list
                                                                           state_.where_conditions_,
                                                                           "AND",
                                                                           false);
            any_user_condition_written_in_block = true;
        }

        if (!state_.or_conditions_.empty()) {
            if (any_user_condition_written_in_block) user_conditions_builder_ss << " OR ";
            QueryBuilder::build_one_condition_block_internal_static_helper(user_conditions_builder_ss, user_conditions_bindings_list, state_.or_conditions_, "OR", false);
            any_user_condition_written_in_block = true;
        }

        if (!state_.not_conditions_.empty()) {
            if (any_user_condition_written_in_block) user_conditions_builder_ss << " AND ";
            QueryBuilder::build_one_condition_block_internal_static_helper(user_conditions_builder_ss, user_conditions_bindings_list, state_.not_conditions_, "AND", true);  // is_not_group = true
                                                                                                                                                                             // any_user_condition_written_in_block = true; // Already true if others were present or becomes true here
        }

        std::string user_conditions_final_sql = user_conditions_builder_ss.str();
        bool main_clause_keyword_written_this_call = false;

        if (!prepended_scope_sql.empty()) {
            if (first_overall_condition_written) {
                // This is for row-level conditions, so always WHERE, not HAVING.
                // The `state_.having_condition_` check was a bug here.
                sql_stream << " WHERE ";
                first_overall_condition_written = false;  // WHERE/HAVING has now been written (or started)
                main_clause_keyword_written_this_call = true;
            } else {
                // A WHERE/HAVING clause was already started by something else, or by a previous scope.
                sql_stream << " AND ";
            }
            sql_stream << "(" << prepended_scope_sql << ")";
        }

        if (!user_conditions_final_sql.empty()) {
            if (first_overall_condition_written) {
                sql_stream << " WHERE ";  // Always WHERE for this context
                first_overall_condition_written = false;
                main_clause_keyword_written_this_call = true;
            } else {
                // If a main keyword (WHERE/HAVING) was written by this call for the scope SQL,
                // or if scope SQL itself was written, we need an AND.
                if (main_clause_keyword_written_this_call || !prepended_scope_sql.empty()) {
                    sql_stream << " AND ";
                } else {
                    // If neither scope SQL nor main keyword was written *by this call*,
                    // but first_overall_condition_written is false (meaning something external to this call
                    // or a prior group within this call already started a WHERE/HAVING clause),
                    // we need an "AND" to connect.
                    // The original complex logic based on last_char was fragile.
                    // A simpler check: if the stream is not empty AND first_overall_condition_written is false,
                    // it implies a prior condition exists and needs "AND".
                    std::string current_sql_in_stream = sql_stream.str();
                    if (!current_sql_in_stream.empty()) {  // Ensure stream is not empty before checking last char
                        char last_significant_char = ' ';
                        for (auto it = current_sql_in_stream.rbegin(); it != current_sql_in_stream.rend(); ++it) {
                            if (*it != ' ') {
                                last_significant_char = *it;
                                break;
                            }
                        }
                        // If it doesn't end with an opening paren or already an operator/space, add AND
                        if (last_significant_char != '(' && last_significant_char != ' ' && !(current_sql_in_stream.length() >= 3 && current_sql_in_stream.substr(current_sql_in_stream.length() - 3) == "AND") &&  // poor check
                            !(current_sql_in_stream.length() >= 2 && current_sql_in_stream.substr(current_sql_in_stream.length() - 2) == "OR")                                                                      // poor check
                        ) {
                            // Add AND if it's not immediately after WHERE/HAVING (which would be handled by first_overall_condition_written)
                            // and not after another logical operator that implies connection.
                            // This part is tricky. A robust solution is for buildSelectSQL to manage the first WHERE/HAVING
                            // and subsequent appends always use AND if needed.
                            // The current state of `first_overall_condition_written` should guide this.
                            // If `first_overall_condition_written` is false, it means `WHERE` or `HAVING` (or `AND/OR` to connect to it)
                            // has already been written by *something*. So we need `AND`.
                            sql_stream << " AND ";
                        }
                    }
                    // If the stream was empty, and first_overall_condition_written is false, it's an invalid state.
                    // This 'else' block is reached if first_overall_condition_written is false (so WHERE/HAVING already exists)
                    // AND main_clause_keyword_written_this_call is false (so this call didn't write WHERE/HAVING for scope)
                    // AND prepended_scope_sql is empty (so scope didn't write AND).
                    // This means we are appending user conditions to an existing WHERE/HAVING.
                    // We *always* need an AND in this case, unless sql_stream is empty (which would be an error if first_overall is false).
                    // The original logic here was complex and potentially buggy.
                    // A clearer approach: if first_overall_condition_written is false, it means a WHERE/HAVING (or its connector)
                    // has been output. Thus, any subsequent condition block *must* be joined by AND/OR.
                    // Since build_condition_logic_internal is for one group (WHERE or HAVING),
                    // if first_overall_condition_written is false, it means we append with AND.
                    // The only exception is if sql_stream is truly empty, which means an error in logic.
                    // Given the `else` implies `!main_clause_keyword_written_this_call` and `prepended_scope_sql.empty()`,
                    // and `!first_overall_condition_written`, it means a previous call or part handled WHERE/HAVING.
                    // So, we just need " AND ".
                    // The complex last_char check can be simplified if the overall structure is managed well.
                    // For now, relying on the refined logic.
                }
            }
            sql_stream << "(" << user_conditions_final_sql << ")";
            bound_params_accumulator.append(user_conditions_bindings_list);  // Append the block's bindings
        }
    }

}  // namespace cpporm