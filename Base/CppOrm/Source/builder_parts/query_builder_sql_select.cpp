// cpporm/builder_parts/query_builder_sql_select.cpp
#include <QDebug>     // For qWarning, qInfo
#include <QMetaType>  // For QMetaType::UnknownType
#include <algorithm>  // For std::tolower in string comparison
#include <sstream>    // For std::ostringstream
#include <variant>    // For std::visit

#include "cpporm/model_base.h"     // For ModelMeta, FieldMeta, FieldFlag
#include "cpporm/query_builder.h"  // Includes QueryBuilderState via query_builder.h -> query_builder_core.h -> ..._state.h

namespace cpporm {

    std::pair<QString, QVariantList> QueryBuilder::buildSelectSQL(bool for_subquery_generation) const {
        std::ostringstream sql_stream;
        QVariantList bound_params_accumulator;  // QueryBuilder still accumulates QVariant bindings internally

        build_ctes_sql_prefix(sql_stream, bound_params_accumulator);

        sql_stream << "SELECT ";
        if (state_.apply_distinct_) {
            sql_stream << "DISTINCT ";
        }

        const std::string &conn_name_std = this->getConnectionName();  // Now std::string

        auto get_field_select_expression = [&conn_name_std, this](const FieldMeta &fm) -> std::string {  // Capture conn_name_std
            // Use helper for case-insensitive check
            bool is_mysql = string_contains_ci(conn_name_std, "mysql") || string_contains_ci(conn_name_std, "mariadb");

            if (is_mysql) {
                if (fm.db_type_hint == "POINT") {
                    return "ST_AsText(" + quoteSqlIdentifier(fm.db_name) + ") AS " + quoteSqlIdentifier(fm.db_name);
                } else if (fm.db_type_hint == "JSON") {
                    // For MySQL JSON, casting to CHAR might be needed for some clients/drivers
                    // if they don't handle JSON type directly from C++ side.
                    // This was likely for QSql which might treat JSON as string.
                    // SqlDriver might handle JSON better, but keeping the cast for now if it was intentional.
                    return "CAST(" + quoteSqlIdentifier(fm.db_name) + " AS CHAR) AS " + quoteSqlIdentifier(fm.db_name);
                }
            }
            // For other drivers or default case
            return quoteSqlIdentifier(fm.db_name);
        };

        if (state_.select_fields_.empty() || (state_.select_fields_.size() == 1 && std::holds_alternative<std::string>(state_.select_fields_[0]) && std::get<std::string>(state_.select_fields_[0]) == "*")) {
            if (state_.model_meta_) {
                bool first_col = true;
                for (const auto &field_meta_obj : state_.model_meta_->fields) {  // Renamed for clarity
                    if (has_flag(field_meta_obj.flags, FieldFlag::Association) || field_meta_obj.db_name.empty()) {
                        continue;
                    }
                    if (!first_col) {
                        sql_stream << ", ";
                    }
                    sql_stream << get_field_select_expression(field_meta_obj);
                    first_col = false;
                }
                if (first_col) {  // No fields were selected from model_meta
                    sql_stream << "*";
                    qWarning(
                        "cpporm QueryBuilder::buildSelectSQL: SELECT * expanded to "
                        "no columns for model %s, falling back to literal '*'.",
                        state_.model_meta_->table_name.c_str());
                }
            } else {
                sql_stream << "*";  // No model_meta, so select literal "*"
            }
        } else {  // Specific fields or subqueries selected
            for (size_t i = 0; i < state_.select_fields_.size(); ++i) {
                std::visit(
                    [&sql_stream, &bound_params_accumulator, this](auto &&arg) {  // `this` needed for quoteSqlIdentifier
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::string>) {
                            sql_stream << arg;  // Assumes string is already quoted or is an expression
                        } else if constexpr (std::is_same_v<T, NamedSubqueryField>) {
                            sql_stream << "(" << arg.subquery.sql_string << ") AS " << quoteSqlIdentifier(arg.alias);  // quoteSqlIdentifier is static
                            // Bindings for subquery in SELECT list are QueryValueVariantForSubquery
                            for (const auto &sub_binding_variant : arg.subquery.bindings) {
                                std::visit(
                                    [&bound_params_accumulator](auto &&sub_val) {
                                        using SubVT = std::decay_t<decltype(sub_val)>;
                                        if constexpr (std::is_same_v<SubVT, std::nullptr_t>) {
                                            bound_params_accumulator.append(QVariant(QMetaType(QMetaType::UnknownType)));
                                        } else if constexpr (std::is_same_v<SubVT, std::string>) {
                                            bound_params_accumulator.append(QString::fromStdString(sub_val));
                                        } else if constexpr (std::is_same_v<SubVT, QDateTime> || std::is_same_v<SubVT, QDate> || std::is_same_v<SubVT, QTime> || std::is_same_v<SubVT, QByteArray> || std::is_same_v<SubVT, bool> || std::is_same_v<SubVT, int> || std::is_same_v<SubVT, long long> ||
                                                             std::is_same_v<SubVT, double>) {
                                            bound_params_accumulator.append(QVariant::fromValue(sub_val));
                                        } else {
                                            qWarning() << "buildSelectSQL (SelectField): Unhandled "
                                                          "native type in NamedSubqueryField "
                                                          "binding during QVariant conversion for subquery in SELECT.";
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
            [&sql_stream, &bound_params_accumulator, this](auto &&arg) {  // `this` needed for getFromSourceName and quoteSqlIdentifier
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) {           // Table name
                    QString table_name_qstr = this->getFromSourceName();  // Returns QString
                    if (table_name_qstr.isEmpty()) {
                        qWarning(
                            "cpporm QueryBuilder: Table name is empty for "
                            "buildSelectSQL FROM clause.");
                        sql_stream << "__MISSING_TABLE_NAME_IN_FROM__";  // Placeholder for error
                    } else {
                        sql_stream << quoteSqlIdentifier(table_name_qstr.toStdString());
                    }
                } else if constexpr (std::is_same_v<T, SubquerySource>) {  // Subquery as source
                    sql_stream << "(" << arg.subquery.sql_string << ") AS " << quoteSqlIdentifier(arg.alias);
                    for (const auto &sub_binding_variant : arg.subquery.bindings) {
                        std::visit(
                            [&bound_params_accumulator](auto &&sub_val) {
                                using SubVT = std::decay_t<decltype(sub_val)>;
                                if constexpr (std::is_same_v<SubVT, std::nullptr_t>) {
                                    bound_params_accumulator.append(QVariant(QMetaType(QMetaType::UnknownType)));
                                } else if constexpr (std::is_same_v<SubVT, std::string>) {
                                    bound_params_accumulator.append(QString::fromStdString(sub_val));
                                } else if constexpr (std::is_same_v<SubVT, QDateTime> || std::is_same_v<SubVT, QDate> || std::is_same_v<SubVT, QTime> || std::is_same_v<SubVT, QByteArray> || std::is_same_v<SubVT, bool> || std::is_same_v<SubVT, int> || std::is_same_v<SubVT, long long> ||
                                                     std::is_same_v<SubVT, double>) {
                                    bound_params_accumulator.append(QVariant::fromValue(sub_val));
                                } else {
                                    qWarning() << "buildSelectSQL (FromClauseSource): "
                                                  "Unhandled native type in SubquerySource "
                                                  "binding during QVariant conversion for FROM subquery.";
                                }
                            },
                            sub_binding_variant);
                    }
                }
            },
            state_.from_clause_source_);

        for (const auto &join : state_.join_clauses_) {
            if (!join.join_type.empty() && !join.table_to_join.empty() && !join.on_condition.empty()) {
                sql_stream << " " << join.join_type << " JOIN " << quoteSqlIdentifier(join.table_to_join) << " ON " << join.on_condition;
            } else if (!join.on_condition.empty()) {  // Raw join fragment
                sql_stream << " " << join.on_condition;
            } else {
                qWarning() << "cpporm QueryBuilder: Invalid join clause for source " << getFromSourceName()  // Returns QString
                           << " (type: " << QString::fromStdString(join.join_type) << ", table: " << QString::fromStdString(join.table_to_join) << ").";
            }
        }

        std::string soft_delete_sql_fragment;
        if (state_.model_meta_ && state_.apply_soft_delete_scope_) {
            bool apply_sd_on_this_from_source = false;
            QString current_from_qstr = this->getFromSourceName();  // Returns QString
            if (!current_from_qstr.isEmpty() && state_.model_meta_->table_name == current_from_qstr.toStdString()) {
                apply_sd_on_this_from_source = true;
            }

            if (apply_sd_on_this_from_source) {
                if (const FieldMeta *deleted_at_field = state_.model_meta_->findFieldWithFlag(FieldFlag::DeletedAt)) {
                    soft_delete_sql_fragment = quoteSqlIdentifier(state_.model_meta_->table_name) + "." + quoteSqlIdentifier(deleted_at_field->db_name) + " IS NULL";
                }
            }
        }

        bool first_condition_written_flag = true;  // True if "WHERE" needs to be written
        build_condition_logic_internal(sql_stream, bound_params_accumulator, first_condition_written_flag, soft_delete_sql_fragment);

        if (!state_.group_clause_.empty()) {
            sql_stream << " GROUP BY " << state_.group_clause_;
            if (state_.having_condition_) {  // Check if having_condition_ unique_ptr is set
                sql_stream << " HAVING ";
                const std::string &having_query_str = state_.having_condition_->query_string;
                const std::vector<QueryValue> &having_args = state_.having_condition_->args;

                std::string::size_type last_pos_having = 0, find_pos_having = 0;
                int arg_idx_having = 0;
                while ((find_pos_having = having_query_str.find('?', last_pos_having)) != std::string::npos) {
                    sql_stream << having_query_str.substr(last_pos_having, find_pos_having - last_pos_having);
                    if (arg_idx_having < static_cast<int>(having_args.size())) {
                        const auto &arg_val_having = having_args[arg_idx_having++];
                        // toQVariant is static and handles SubqueryExpression by adding its bindings to accumulator
                        QVariant qv_arg_having = QueryBuilder::toQVariant(arg_val_having, bound_params_accumulator);
                        if (std::holds_alternative<SubqueryExpression>(arg_val_having)) {
                            sql_stream << qv_arg_having.toString().toStdString();  // Injects "(subquery_sql)"
                        } else {
                            sql_stream << "?";  // Placeholder for regular value
                        }
                    } else {
                        sql_stream << "?";
                        qWarning() << "cpporm: Not enough arguments for placeholders in "
                                      "HAVING clause: "
                                   << QString::fromStdString(having_query_str);
                    }
                    last_pos_having = find_pos_having + 1;
                }
                sql_stream << having_query_str.substr(last_pos_having);
                if (arg_idx_having < static_cast<int>(having_args.size())) {
                    bool only_subqueries_left_having = true;
                    for (size_t k_having = arg_idx_having; k_having < having_args.size(); ++k_having) {
                        if (!std::holds_alternative<SubqueryExpression>(having_args[k_having])) {
                            only_subqueries_left_having = false;
                            break;
                        }
                    }
                    if (!only_subqueries_left_having) {
                        qWarning() << "cpporm: Too many non-subquery arguments for "
                                      "placeholders in HAVING clause: "
                                   << QString::fromStdString(having_query_str);
                    }
                }
            }
        }

        if (!state_.order_clause_.empty()) {
            sql_stream << " ORDER BY " << state_.order_clause_;
        }

        // LIMIT and OFFSET are not applied if for_subquery_generation is true
        if (!for_subquery_generation) {
            if (state_.limit_val_ > 0) {
                sql_stream << " LIMIT ?";
                bound_params_accumulator.append(QVariant::fromValue(state_.limit_val_));  // Use QVariant::fromValue for clarity
                if (state_.offset_val_ >= 0) {                                            // Only add OFFSET if limit is also present and offset is valid
                    sql_stream << " OFFSET ?";
                    bound_params_accumulator.append(QVariant::fromValue(state_.offset_val_));
                }
            } else if (state_.offset_val_ >= 0) {  // OFFSET without LIMIT (some DBs need a large LIMIT)
                // const std::string& connNameStd = getConnectionName(); // Already fetched
                if (string_contains_ci(conn_name_std, "mysql")) {
                    sql_stream << " LIMIT 18446744073709551615";  // MySQL's max rows
                }
                sql_stream << " OFFSET ?";
                bound_params_accumulator.append(QVariant::fromValue(state_.offset_val_));
                if (!string_contains_ci(conn_name_std, "mysql") && !string_contains_ci(conn_name_std, "sqlite")) {
                    // This warning is fine, just for developer awareness
                    qInfo(
                        "cpporm QueryBuilder: OFFSET without LIMIT is used for driver '%s'. "
                        "Behavior might vary. MySQL and SQLite effectively add a large LIMIT.",
                        conn_name_std.c_str());
                }
            }
        }
        return {QString::fromStdString(sql_stream.str()), bound_params_accumulator};
    }

}  // namespace cpporm