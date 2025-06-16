// cpporm/builder_parts/query_builder_helpers.cpp
#include "cpporm/model_base.h" // For ModelMeta related logic
#include "cpporm/query_builder.h" // For QueryBuilder class and its members like quoteSqlIdentifier, toQVariant

#include <QDebug>
#include <QMetaType> // For QMetaType::UnknownType
#include <QVariant>
#include <sstream>
#include <variant> // For std::visit

namespace cpporm {

// --- Private SQL Building Helper Implementations for QueryBuilder ---

void QueryBuilder::build_ctes_sql_prefix(
    std::ostringstream &sql_stream,
    QVariantList &bound_params_accumulator) const {
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
              bound_params_accumulator.append(
                  QVariant(QMetaType(QMetaType::UnknownType)));
            } else if constexpr (std::is_same_v<ArgT, std::string>) {
              bound_params_accumulator.append(QString::fromStdString(arg_val));
            } else if constexpr (std::is_same_v<ArgT, int> ||
                                 std::is_same_v<ArgT, long long> ||
                                 std::is_same_v<ArgT, double> ||
                                 std::is_same_v<ArgT, bool> ||
                                 std::is_same_v<ArgT, QDateTime> ||
                                 std::is_same_v<ArgT, QDate> ||
                                 std::is_same_v<ArgT, QTime> ||
                                 std::is_same_v<ArgT, QByteArray>) {
              bound_params_accumulator.append(QVariant::fromValue(arg_val));
            } else {
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

bool QueryBuilder::build_one_condition_block_internal_static_helper(
    std::ostringstream &to_stream, QVariantList &bindings_acc,
    const std::vector<Condition> &conditions_group,
    const std::string &op_within_group, bool is_not_group) {

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

    while ((find_pos = local_query_string.find('?', last_pos)) !=
           std::string::npos) {
      to_stream << local_query_string.substr(last_pos, find_pos - last_pos);
      if (arg_idx < static_cast<int>(local_args.size())) {
        const auto &arg_value = local_args[arg_idx++];
        QVariant qv_arg = QueryBuilder::toQVariant(arg_value, bindings_acc);
        if (std::holds_alternative<SubqueryExpression>(arg_value)) {
          to_stream << qv_arg.toString().toStdString();
        } else {
          to_stream << "?";
          bindings_acc.append(qv_arg);
        }
      } else {
        qWarning() << "cpporm: Not enough arguments for placeholders in "
                      "condition string:"
                   << QString::fromStdString(local_query_string);
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
        qWarning() << "cpporm: Too many non-subquery arguments for "
                      "placeholders in condition string:"
                   << QString::fromStdString(local_query_string);
      }
    }
  }
  to_stream << ")";
  return true;
}

void QueryBuilder::build_condition_logic_internal(
    std::ostringstream &sql_stream, QVariantList &bound_params_accumulator,
    bool &first_overall_condition_written,
    const std::string &prepended_scope_sql) const {

  std::ostringstream user_conditions_builder_ss;
  QVariantList user_conditions_bindings_list;
  // bool has_where_conditions = false; // Not used in current logic
  // bool has_or_conditions = false;    // Not used in current logic
  bool any_user_condition_written = false;

  if (!state_.where_conditions_.empty()) {
    if (any_user_condition_written) // Should always be false on first entry
                                    // here
      user_conditions_builder_ss << " AND ";
    QueryBuilder::build_one_condition_block_internal_static_helper(
        user_conditions_builder_ss, user_conditions_bindings_list,
        state_.where_conditions_, "AND", false);
    // has_where_conditions = true; // Not used
    any_user_condition_written = true;
  }

  if (!state_.or_conditions_.empty()) {
    if (any_user_condition_written) // Could be true if where_conditions_ was
                                    // processed
      user_conditions_builder_ss << " OR ";
    QueryBuilder::build_one_condition_block_internal_static_helper(
        user_conditions_builder_ss, user_conditions_bindings_list,
        state_.or_conditions_, "OR", false);
    // has_or_conditions = true; // Not used
    any_user_condition_written = true;
  }

  if (!state_.not_conditions_.empty()) {
    if (any_user_condition_written) // Could be true if where_conditions_ or
                                    // or_conditions_ was processed
      user_conditions_builder_ss << " AND ";
    QueryBuilder::build_one_condition_block_internal_static_helper(
        user_conditions_builder_ss, user_conditions_bindings_list,
        state_.not_conditions_, "AND", true);
    any_user_condition_written = true;
  }

  std::string user_conditions_final_sql = user_conditions_builder_ss.str();
  bool main_clause_keyword_written_this_call = false;

  if (!prepended_scope_sql.empty()) {
    if (first_overall_condition_written) {
      // sql_stream << (state_.having_condition_ ? " HAVING " : " WHERE "); //
      // ORIGINAL BUGGY LINE
      sql_stream
          << " WHERE "; // FIXED: Always use WHERE for row-level conditions
      first_overall_condition_written = false;
      main_clause_keyword_written_this_call = true;
    } else {
      sql_stream << " AND ";
    }
    sql_stream << "(" << prepended_scope_sql << ")";
  }

  if (!user_conditions_final_sql.empty()) {
    if (first_overall_condition_written) {
      // sql_stream << (state_.having_condition_ ? " HAVING " : " WHERE "); //
      // ORIGINAL BUGGY LINE
      sql_stream
          << " WHERE "; // FIXED: Always use WHERE for row-level conditions
      first_overall_condition_written = false;
      main_clause_keyword_written_this_call = true;
    } else {
      // This complex 'else' block tries to decide if "AND" is needed.
      // A simpler approach if main_clause_keyword_written_this_call or
      // !prepended_scope_sql.empty() is true means that a condition (or the
      // "WHERE" keyword) was already written by *this function call* or for
      // *this scope*, thus "AND" is appropriate.
      if (main_clause_keyword_written_this_call ||
          !prepended_scope_sql.empty()) {
        sql_stream << " AND ";
      } else {
        // If neither scope SQL nor main keyword was written *by this call*,
        // but first_overall_condition_written is false (meaning something
        // external already started a WHERE clause), we still need an "AND". The
        // original complex logic might be trying to be too smart. A robust
        // solution might be to ensure `buildSelectSQL` manages the absolute
        // first `WHERE` and subsequent calls to
        // `build_condition_logic_internal` (if any for different groups, though
        // not current design) or internal parts always prepend `AND`. For now,
        // the original logic for this specific 'else' path:
        std::string current_sql = sql_stream.str();
        if (!current_sql.empty()) {
          char last_char = ' ';
          for (auto it = current_sql.rbegin(); it != current_sql.rend(); ++it) {
            if (*it != ' ') {
              last_char = *it;
              break;
            }
          }
          // If the stream doesn't end with an opening parenthesis or a space,
          // it implies we need an AND to connect to previous content.
          if (last_char != '(' && last_char != ' ') {
            sql_stream << " AND ";
          }
          // This condition seems redundant if the above 'if' handles
          // !prepended_scope_sql.empty() else if (last_char == ' ' &&
          // !prepended_scope_sql.empty()) {
          //   sql_stream << " AND ";
          // }
        }
      }
    }
    sql_stream << "(" << user_conditions_final_sql << ")";
    bound_params_accumulator.append(user_conditions_bindings_list);
  }
}

} // namespace cpporm