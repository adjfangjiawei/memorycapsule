#include "cpporm/model_base.h"    // For ModelMeta, FieldMeta, FieldFlag
#include "cpporm/query_builder.h" // Includes QueryBuilderState via query_builder.h -> query_builder_core.h -> ..._state.h

#include <QDebug>
#include <QMetaType>
#include <sstream>
#include <variant> // For std::visit

namespace cpporm {

std::pair<QString, QVariantList>
QueryBuilder::buildSelectSQL(bool for_subquery_generation) const {
  std::ostringstream sql_stream;
  QVariantList bound_params_accumulator;

  build_ctes_sql_prefix(sql_stream, bound_params_accumulator);

  sql_stream << "SELECT ";
  if (state_.apply_distinct_) { // Check if Distinct() was called
    sql_stream << "DISTINCT ";
  }

  auto get_field_select_expression =
      [this](const FieldMeta &fm) -> std::string {
    const QString &conn_name = this->getConnectionName();
    bool is_mysql = conn_name.contains("mysql", Qt::CaseInsensitive) ||
                    conn_name.contains("mariadb", Qt::CaseInsensitive);

    if (is_mysql) {
      if (fm.db_type_hint == "POINT") {
        return "ST_AsText(" + quoteSqlIdentifier(fm.db_name) + ") AS " +
               quoteSqlIdentifier(fm.db_name);
      } else if (fm.db_type_hint == "JSON") {
        return "CAST(" + quoteSqlIdentifier(fm.db_name) + " AS CHAR) AS " +
               quoteSqlIdentifier(fm.db_name);
      }
    }
    return quoteSqlIdentifier(fm.db_name);
  };

  if (state_.select_fields_.empty() ||
      (state_.select_fields_.size() == 1 &&
       std::holds_alternative<std::string>(state_.select_fields_[0]) &&
       std::get<std::string>(state_.select_fields_[0]) == "*")) {
    if (state_.model_meta_) {
      bool first_col = true;
      for (const auto &field_meta : state_.model_meta_->fields) {
        if (has_flag(field_meta.flags, FieldFlag::Association) ||
            field_meta.db_name.empty()) {
          continue;
        }
        if (!first_col) {
          sql_stream << ", ";
        }
        sql_stream << get_field_select_expression(field_meta);
        first_col = false;
      }
      if (first_col) {
        sql_stream << "*";
        qWarning("cpporm QueryBuilder::buildSelectSQL: SELECT * expanded to "
                 "no columns for model %s, falling back to literal '*'.",
                 state_.model_meta_->table_name.c_str());
      }
    } else {
      sql_stream << "*";
    }
  } else {
    for (size_t i = 0; i < state_.select_fields_.size(); ++i) {
      std::visit(
          [&sql_stream, &bound_params_accumulator, this](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
              sql_stream << arg;
            } else if constexpr (std::is_same_v<T, NamedSubqueryField>) {
              sql_stream << "(" << arg.subquery.sql_string << ") AS "
                         << quoteSqlIdentifier(arg.alias);
              for (const auto &sub_binding_variant : arg.subquery.bindings) {
                std::visit(
                    [&bound_params_accumulator](auto &&sub_val) {
                      using SubVT = std::decay_t<decltype(sub_val)>;
                      if constexpr (std::is_same_v<SubVT, std::nullptr_t>) {
                        bound_params_accumulator.append(
                            QVariant(QMetaType(QMetaType::UnknownType)));
                      } else if constexpr (std::is_same_v<SubVT, std::string>) {
                        bound_params_accumulator.append(
                            QString::fromStdString(sub_val));
                      } else if constexpr (std::is_same_v<SubVT, QDateTime> ||
                                           std::is_same_v<SubVT, QDate> ||
                                           std::is_same_v<SubVT, QTime> ||
                                           std::is_same_v<SubVT, QByteArray> ||
                                           std::is_same_v<SubVT, bool> ||
                                           std::is_same_v<SubVT, int> ||
                                           std::is_same_v<SubVT, long long> ||
                                           std::is_same_v<SubVT, double>) {
                        bound_params_accumulator.append(
                            QVariant::fromValue(sub_val));
                      } else {
                        qWarning() << "buildSelectSQL (SelectField): Unhandled "
                                      "native type in NamedSubqueryField "
                                      "binding during QVariant conversion.";
                      }
                    },
                    sub_binding_variant);
              }
            }
          },
          state_.select_fields_[i]);
      if (i < state_.select_fields_.size() - 1) {
        sql_stream << ", ";
      }
    }
  }

  sql_stream << " FROM ";
  std::visit(
      [&sql_stream, &bound_params_accumulator, this](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
          QString table_name_qstr = this->getFromSourceName();
          if (table_name_qstr.isEmpty()) {
            qWarning("cpporm QueryBuilder: Table name is empty for "
                     "buildSelectSQL FROM clause.");
            sql_stream << "__MISSING_TABLE_NAME_IN_FROM__";
          } else {
            sql_stream << quoteSqlIdentifier(table_name_qstr.toStdString());
          }
        } else if constexpr (std::is_same_v<T, SubquerySource>) {
          sql_stream << "(" << arg.subquery.sql_string << ") AS "
                     << quoteSqlIdentifier(arg.alias);
          for (const auto &sub_binding_variant : arg.subquery.bindings) {
            std::visit(
                [&bound_params_accumulator](auto &&sub_val) {
                  using SubVT = std::decay_t<decltype(sub_val)>;
                  if constexpr (std::is_same_v<SubVT, std::nullptr_t>) {
                    bound_params_accumulator.append(
                        QVariant(QMetaType(QMetaType::UnknownType)));
                  } else if constexpr (std::is_same_v<SubVT, std::string>) {
                    bound_params_accumulator.append(
                        QString::fromStdString(sub_val));
                  } else if constexpr (std::is_same_v<SubVT, QDateTime> ||
                                       std::is_same_v<SubVT, QDate> ||
                                       std::is_same_v<SubVT, QTime> ||
                                       std::is_same_v<SubVT, QByteArray> ||
                                       std::is_same_v<SubVT, bool> ||
                                       std::is_same_v<SubVT, int> ||
                                       std::is_same_v<SubVT, long long> ||
                                       std::is_same_v<SubVT, double>) {
                    bound_params_accumulator.append(
                        QVariant::fromValue(sub_val));
                  } else {
                    qWarning() << "buildSelectSQL (FromClauseSource): "
                                  "Unhandled native type in SubquerySource "
                                  "binding during QVariant conversion.";
                  }
                },
                sub_binding_variant);
          }
        }
      },
      state_.from_clause_source_);

  for (const auto &join : state_.join_clauses_) {
    if (!join.join_type.empty() && !join.table_to_join.empty() &&
        !join.on_condition.empty()) {
      sql_stream << " " << join.join_type << " JOIN "
                 << quoteSqlIdentifier(join.table_to_join) << " ON "
                 << join.on_condition;
    } else if (!join.on_condition.empty()) {
      sql_stream << " " << join.on_condition;
    } else {
      qWarning() << "cpporm QueryBuilder: Invalid join clause for source"
                 << getFromSourceName()
                 << "(type:" << QString::fromStdString(join.join_type)
                 << ", table:" << QString::fromStdString(join.table_to_join)
                 << ").";
    }
  }

  std::string soft_delete_sql_fragment;
  if (state_.model_meta_ && state_.apply_soft_delete_scope_) {
    bool apply_sd_on_this_from_source = false;
    QString current_from_qstr = this->getFromSourceName();
    if (!current_from_qstr.isEmpty() &&
        state_.model_meta_->table_name == current_from_qstr.toStdString()) {
      apply_sd_on_this_from_source = true;
    }

    if (apply_sd_on_this_from_source) {
      if (const FieldMeta *deleted_at_field =
              state_.model_meta_->findFieldWithFlag(FieldFlag::DeletedAt)) {
        soft_delete_sql_fragment =
            quoteSqlIdentifier(state_.model_meta_->table_name) + "." +
            quoteSqlIdentifier(deleted_at_field->db_name) + " IS NULL";
      }
    }
  }

  bool first_condition_written_flag = true;
  build_condition_logic_internal(sql_stream, bound_params_accumulator,
                                 first_condition_written_flag,
                                 soft_delete_sql_fragment);

  if (!state_.group_clause_.empty()) {
    sql_stream << " GROUP BY " << state_.group_clause_;
    if (state_.having_condition_) {
      sql_stream << " HAVING ";
      const std::string &having_query_str =
          state_.having_condition_->query_string;
      const std::vector<QueryValue> &having_args =
          state_.having_condition_->args;
      std::string::size_type last_pos = 0, find_pos = 0;
      int arg_idx = 0;
      while ((find_pos = having_query_str.find('?', last_pos)) !=
             std::string::npos) {
        sql_stream << having_query_str.substr(last_pos, find_pos - last_pos);
        if (arg_idx < static_cast<int>(having_args.size())) {
          const auto &arg_val = having_args[arg_idx++];
          if (std::holds_alternative<SubqueryExpression>(arg_val)) {
            sql_stream << QueryBuilder::toQVariant(arg_val,
                                                   bound_params_accumulator)
                              .toString()
                              .toStdString();
          } else {
            sql_stream << "?";
            bound_params_accumulator.append(
                QueryBuilder::toQVariant(arg_val, bound_params_accumulator));
          }
        } else {
          sql_stream << "?";
          qWarning() << "cpporm: Not enough arguments for placeholders in "
                        "HAVING clause:"
                     << QString::fromStdString(having_query_str);
        }
        last_pos = find_pos + 1;
      }
      sql_stream << having_query_str.substr(last_pos);
      if (arg_idx < static_cast<int>(having_args.size())) {
        bool only_subqueries_left = true;
        for (size_t k = arg_idx; k < having_args.size(); ++k) {
          if (!std::holds_alternative<SubqueryExpression>(having_args[k])) {
            only_subqueries_left = false;
            break;
          }
        }
        if (!only_subqueries_left) {
          qWarning() << "cpporm: Too many non-subquery arguments for "
                        "placeholders in HAVING clause:"
                     << QString::fromStdString(having_query_str);
        }
      }
    }
  }

  if (!state_.order_clause_.empty()) {
    sql_stream << " ORDER BY " << state_.order_clause_;
  }

  if (!for_subquery_generation) {
    if (state_.limit_val_ > 0) {
      sql_stream << " LIMIT ?";
      bound_params_accumulator.append(state_.limit_val_);
      if (state_.offset_val_ >= 0) {
        sql_stream << " OFFSET ?";
        bound_params_accumulator.append(state_.offset_val_);
      }
    } else if (state_.offset_val_ >= 0) {
      if (getConnectionName().contains("mysql", Qt::CaseInsensitive)) {
        sql_stream << " LIMIT 18446744073709551615";
      }
      sql_stream << " OFFSET ?";
      bound_params_accumulator.append(state_.offset_val_);
      if (!getConnectionName().contains("mysql", Qt::CaseInsensitive) &&
          !getConnectionName().contains("sqlite", Qt::CaseInsensitive)) {
        qWarning("cpporm QueryBuilder: OFFSET without LIMIT is used. Behavior "
                 "might vary by RDBMS. MySQL and SQLite require a LIMIT.");
      }
    }
  }
  return {QString::fromStdString(sql_stream.str()), bound_params_accumulator};
}

} // namespace cpporm