// cpporm/builder_parts/query_builder_sql_insert.cpp
#include <QDebug>     // For qWarning
#include <algorithm>  // For std::tolower in string comparison (if needed for driver name)
#include <sstream>    // For std::ostringstream
#include <variant>    // For std::holds_alternative in QueryBuilder::toQVariant

#include "cpporm/model_base.h"  // For ModelMeta, FieldMeta for OnConflictUpdateAllExcluded
#include "cpporm/query_builder.h"

namespace cpporm {

    // Helper (already defined in query_builder_sql_select.cpp, ideally should be in a common util or QueryBuilder itself if static)
    // For now, let's assume it's accessible or re-define locally if necessary.
    // bool string_contains_ci(const std::string& text, const std::string& pattern); // Declaration

    // This is the SOLE definition of buildInsertSQLSuffix
    std::pair<QString, QVariantList> QueryBuilder::buildInsertSQLSuffix(const std::vector<std::string> &inserted_columns_db_names_for_values_clause) const {
        std::ostringstream sql_suffix_stream;
        QVariantList suffix_bindings_accumulator;

        if (!state_.on_conflict_clause_) {
            return {QString(), QVariantList()};
        }

        std::string driver_name_upper_std;                       // Store as std::string
        const std::string &conn_name_std = getConnectionName();  // std::string

        // Simplified driver detection based on connection name content
        // This is a basic heuristic and might need refinement for robustness.
        auto to_upper_std_string = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return std::toupper(c);
            });
            return s;
        };

        // Check based on common substrings in connection_name (heuristic)
        // Or, ideally, Session would pass a hint or the driver itself would be queried.
        // For now, sticking to the string contains logic for simplicity.
        if (string_contains_ci(conn_name_std, "psql") || string_contains_ci(conn_name_std, "postgres")) {
            driver_name_upper_std = "QPSQL";  // Using Qt-like names as placeholders for logic
        } else if (string_contains_ci(conn_name_std, "mysql") || string_contains_ci(conn_name_std, "mariadb")) {
            driver_name_upper_std = "QMYSQL";
        } else if (string_contains_ci(conn_name_std, "sqlite")) {
            driver_name_upper_std = "QSQLITE";
        }
        // Add other dialects if needed

        if (driver_name_upper_std.empty()) {
            // Fallback if connection_name_ doesn't give a hint.
            // qWarning() << "QueryBuilder::buildInsertSQLSuffix: Could not reliably determine SQL dialect from connection name '"
            //            << QString::fromStdString(conn_name_std) << "'. Defaulting to MySQL-like syntax for suffix.";
            driver_name_upper_std = "QMYSQL";  // Default to MySQL behavior if unknown
        }

        if (state_.on_conflict_clause_->action == OnConflictClause::Action::DoNothing) {
            if (driver_name_upper_std == "QMYSQL") {  // MySQL, MariaDB
                // MySQL uses "INSERT IGNORE", which is typically handled by modifying the main "INSERT" verb,
                // not by a suffix. So, this suffix part should be empty for "DoNothing" on MySQL.
                // The caller (Session::CreateImpl or Session's batch create) should adjust "INSERT" to "INSERT IGNORE".
                return {QString(), QVariantList()};
            } else if (driver_name_upper_std == "QPSQL") {  // PostgreSQL
                sql_suffix_stream << " ON CONFLICT";
                if (!state_.on_conflict_clause_->conflict_target_columns_db_names.empty()) {
                    sql_suffix_stream << " (";
                    for (size_t i = 0; i < state_.on_conflict_clause_->conflict_target_columns_db_names.size(); ++i) {
                        sql_suffix_stream << quoteSqlIdentifier(state_.on_conflict_clause_->conflict_target_columns_db_names[i]);
                        if (i < state_.on_conflict_clause_->conflict_target_columns_db_names.size() - 1) sql_suffix_stream << ", ";
                    }
                    sql_suffix_stream << ")";
                }
                // If no specific target columns, PG might require ON CONSTRAINT name.
                // GORM often requires specifying a target for PG ON CONFLICT DO NOTHING.
                // For simplicity, if no target, assume general DO NOTHING (might fail on some PG setups without target/constraint).
                sql_suffix_stream << " DO NOTHING";
            } else if (driver_name_upper_std == "QSQLITE") {  // SQLite
                // SQLite uses "INSERT OR IGNORE" or "ON CONFLICT DO NOTHING"
                // "INSERT OR IGNORE" is a verb modification.
                // "ON CONFLICT ... DO NOTHING" is a suffix.
                // Let's assume Session will handle "INSERT OR IGNORE". If suffix is desired:
                sql_suffix_stream << " ON CONFLICT";
                if (!state_.on_conflict_clause_->conflict_target_columns_db_names.empty()) {
                    sql_suffix_stream << " (";
                    for (size_t i = 0; i < state_.on_conflict_clause_->conflict_target_columns_db_names.size(); ++i) {
                        sql_suffix_stream << quoteSqlIdentifier(state_.on_conflict_clause_->conflict_target_columns_db_names[i]);
                        if (i < state_.on_conflict_clause_->conflict_target_columns_db_names.size() - 1) sql_suffix_stream << ", ";
                    }
                    sql_suffix_stream << ")";
                }
                sql_suffix_stream << " DO NOTHING";
            }
            // Other dialects might have different syntaxes or no direct "DO NOTHING" via suffix.
        } else if (state_.on_conflict_clause_->action == OnConflictClause::Action::UpdateAllExcluded || state_.on_conflict_clause_->action == OnConflictClause::Action::UpdateSpecific) {
            bool first_update_col = true;
            if (driver_name_upper_std == "QMYSQL") {  // MySQL, MariaDB
                sql_suffix_stream << " ON DUPLICATE KEY UPDATE ";
            } else if (driver_name_upper_std == "QPSQL") {  // PostgreSQL
                sql_suffix_stream << " ON CONFLICT ";
                std::vector<std::string> pg_conflict_targets;
                if (!state_.on_conflict_clause_->conflict_target_columns_db_names.empty()) {
                    pg_conflict_targets = state_.on_conflict_clause_->conflict_target_columns_db_names;
                } else if (state_.model_meta_ && !state_.model_meta_->primary_keys_db_names.empty()) {
                    pg_conflict_targets = state_.model_meta_->primary_keys_db_names;
                }

                if (!pg_conflict_targets.empty()) {
                    sql_suffix_stream << "(";
                    for (size_t i = 0; i < pg_conflict_targets.size(); ++i) {
                        sql_suffix_stream << quoteSqlIdentifier(pg_conflict_targets[i]);
                        if (i < pg_conflict_targets.size() - 1) sql_suffix_stream << ", ";
                    }
                    sql_suffix_stream << ") ";
                } else {
                    qWarning(
                        "cpporm QueryBuilder: For PostgreSQL ON CONFLICT DO UPDATE, "
                        "conflict target (columns or PK) must be defined.");
                    return {QString(), QVariantList()};  // Error condition
                }
                sql_suffix_stream << "DO UPDATE SET ";
            } else if (driver_name_upper_std == "QSQLITE") {  // SQLite
                sql_suffix_stream << " ON CONFLICT";
                // SQLite needs conflict target for DO UPDATE similar to PG
                std::vector<std::string> sqlite_conflict_targets;
                if (!state_.on_conflict_clause_->conflict_target_columns_db_names.empty()) {
                    sqlite_conflict_targets = state_.on_conflict_clause_->conflict_target_columns_db_names;
                } else if (state_.model_meta_ && !state_.model_meta_->primary_keys_db_names.empty()) {
                    sqlite_conflict_targets = state_.model_meta_->primary_keys_db_names;
                }
                if (!sqlite_conflict_targets.empty()) {
                    sql_suffix_stream << " (";
                    for (size_t i = 0; i < sqlite_conflict_targets.size(); ++i) {
                        sql_suffix_stream << quoteSqlIdentifier(sqlite_conflict_targets[i]);
                        if (i < sqlite_conflict_targets.size() - 1) sql_suffix_stream << ", ";
                    }
                    sql_suffix_stream << ")";
                }  // SQLite can also infer from PK/UNIQUE constraints if target omitted
                sql_suffix_stream << " DO UPDATE SET ";
            } else {  // Fallback for other/unknown drivers
                qWarning() << "QueryBuilder::buildInsertSQLSuffix: ON CONFLICT UPDATE behavior for driver '" << QString::fromStdString(driver_name_upper_std) << "' is not specifically handled. Defaulting to MySQL-like 'ON DUPLICATE KEY UPDATE'.";
                sql_suffix_stream << " ON DUPLICATE KEY UPDATE ";
            }

            if (state_.on_conflict_clause_->action == OnConflictClause::Action::UpdateAllExcluded) {
                if (inserted_columns_db_names_for_values_clause.empty() && driver_name_upper_std != "QPSQL" && driver_name_upper_std != "QSQLITE" /*PG/SQLite can use EXCLUDED/excluded even with empty insert list with DEFAULT VALUES*/) {
                    qWarning(
                        "cpporm QueryBuilder: OnConflictUpdateAllExcluded specified for non-PG/non-SQLite, "
                        "but no columns provided from INSERT part to determine VALUES() updates.");
                }
                for (const std::string &db_col_name : inserted_columns_db_names_for_values_clause) {
                    bool skip_this_column_in_set = false;
                    // Skip updating PKs or conflict target columns themselves in the SET clause
                    if (state_.model_meta_) {
                        const auto &conflict_targets_to_check = (driver_name_upper_std == "QPSQL" || driver_name_upper_std == "QSQLITE")
                                                                    ? (state_.on_conflict_clause_->conflict_target_columns_db_names.empty() ? state_.model_meta_->primary_keys_db_names : state_.on_conflict_clause_->conflict_target_columns_db_names)
                                                                    : state_.model_meta_->primary_keys_db_names;  // For MySQL, skip PKs

                        for (const auto &key_col_name : conflict_targets_to_check) {
                            if (key_col_name == db_col_name) {
                                skip_this_column_in_set = true;
                                break;
                            }
                        }
                        // MySQL also skips PKs if not already caught by conflict_targets_to_check
                        if (driver_name_upper_std == "QMYSQL" && !skip_this_column_in_set) {
                            for (const auto &pk_name : state_.model_meta_->primary_keys_db_names) {
                                if (pk_name == db_col_name) {
                                    skip_this_column_in_set = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (skip_this_column_in_set) continue;

                    if (!first_update_col) sql_suffix_stream << ", ";
                    sql_suffix_stream << quoteSqlIdentifier(db_col_name) << " = ";

                    if (driver_name_upper_std == "QMYSQL") {
                        sql_suffix_stream << "VALUES(" << quoteSqlIdentifier(db_col_name) << ")";
                    } else if (driver_name_upper_std == "QPSQL" || driver_name_upper_std == "QSQLITE") {
                        sql_suffix_stream << "excluded." << quoteSqlIdentifier(db_col_name);
                    } else {                       // Fallback for other drivers
                        sql_suffix_stream << "?";  // This would require the value to be bound, which is not typical for UpdateAllExcluded
                        qWarning(
                            "QueryBuilder::buildInsertSQLSuffix: UpdateAllExcluded for driver '%s' "
                            "might need specific value passing or is not fully supported by this generic builder. Using '?' for column '%s'.",
                            driver_name_upper_std.c_str(),
                            db_col_name.c_str());
                    }
                    first_update_col = false;
                }
                if (first_update_col && !inserted_columns_db_names_for_values_clause.empty()) {
                    qWarning("cpporm QueryBuilder: OnConflictUpdateAllExcluded resulted in empty SET clause. SQL might be invalid.");
                }
            } else if (state_.on_conflict_clause_->action == OnConflictClause::Action::UpdateSpecific) {
                if (state_.on_conflict_clause_->update_assignments.empty()) {
                    qWarning("cpporm QueryBuilder: OnConflictUpdateSpecific specified, but no update assignments provided.");
                }
                for (const auto &assign_pair : state_.on_conflict_clause_->update_assignments) {
                    // For PG/SQLite, ensure the column being SET is not part of the conflict target itself
                    if ((driver_name_upper_std == "QPSQL" || driver_name_upper_std == "QSQLITE") && state_.model_meta_) {
                        const auto &conflict_targets = state_.on_conflict_clause_->conflict_target_columns_db_names.empty() ? state_.model_meta_->primary_keys_db_names : state_.on_conflict_clause_->conflict_target_columns_db_names;
                        bool is_conflict_target_col = false;
                        for (const auto &ct_col : conflict_targets) {
                            if (ct_col == assign_pair.first) {
                                is_conflict_target_col = true;
                                break;
                            }
                        }
                        if (is_conflict_target_col) {
                            qWarning() << "QueryBuilder::buildInsertSQLSuffix (" << QString::fromStdString(driver_name_upper_std) << "): Column '" << QString::fromStdString(assign_pair.first) << "' is part of the conflict target and cannot be in the SET clause of ON CONFLICT DO UPDATE. Skipping.";
                            continue;
                        }
                    }

                    if (!first_update_col) sql_suffix_stream << ", ";
                    sql_suffix_stream << quoteSqlIdentifier(assign_pair.first) << " = ";

                    if (std::holds_alternative<SubqueryExpression>(assign_pair.second)) {
                        // toQVariant is static, appends subquery bindings to suffix_bindings_accumulator
                        sql_suffix_stream << QueryBuilder::toQVariant(assign_pair.second, suffix_bindings_accumulator).toString().toStdString();  // Injects "(subquery_sql)"
                    } else {
                        sql_suffix_stream << "?";
                        // toQVariant here just converts the value, does not add to accumulator.
                        // The accumulator is for subquery bindings within the expression.
                        // Regular values are added here.
                        suffix_bindings_accumulator.append(QueryBuilder::toQVariant(assign_pair.second, suffix_bindings_accumulator));
                    }
                    first_update_col = false;
                }
            }

            if (first_update_col &&  // No assignments were actually made
                (state_.on_conflict_clause_->action == OnConflictClause::Action::UpdateAllExcluded ||
                 (state_.on_conflict_clause_->action == OnConflictClause::Action::UpdateSpecific && !state_.on_conflict_clause_->update_assignments.empty() /* only if user provided assignments but all were skipped */))) {
                qWarning(
                    "cpporm QueryBuilder::buildInsertSQLSuffix: Resulted in an ON CONFLICT UPDATE clause "
                    "with no actual assignments. SQL may be invalid or action ineffective.");
                // Depending on DB, an empty SET might be an error or a no-op.
                // For safety, we might return an empty suffix to indicate this problematic state.
                // For now, let it pass, but this is a strong indicator of potential issues.
                // If driver_name_upper_std is PG or SQLite, an empty SET is usually an error.
                if (driver_name_upper_std == "QPSQL" || driver_name_upper_std == "QSQLITE") {
                    return {QString(), QVariantList()};  // Return empty to signal failure for PG/SQLite
                }
            }
        }

        return {QString::fromStdString(sql_suffix_stream.str()), suffix_bindings_accumulator};
    }

}  // namespace cpporm