// cpporm/builder_parts/query_builder_sql_insert.cpp
#include "cpporm/model_base.h" // For ModelMeta, FieldMeta for OnConflictUpdateAllExcluded
#include "cpporm/query_builder.h"

#include <QDebug>
#include <sstream>
#include <variant> // For std::holds_alternative in QueryBuilder::toQVariant

namespace cpporm {

// This is the SOLE definition of buildInsertSQLSuffix
std::pair<QString, QVariantList> QueryBuilder::buildInsertSQLSuffix(
    const std::vector<std::string> &inserted_columns_db_names_for_values_clause)
    const {
  std::ostringstream sql_suffix_stream;
  QVariantList suffix_bindings_accumulator;

  if (!state_.on_conflict_clause_) {
    return {QString(), QVariantList()}; // No ON CONFLICT clause defined
  }

  QString driver_name_upper;
  // Attempt to get driver name hint from connection_name_ if QueryBuilder
  // stores it. This is a simplification; robust dialect handling is complex for
  // a standalone QB. Session should ideally be the one to finalize
  // dialect-specific parts.
  if (!connection_name_.isEmpty()) {
    if (connection_name_.contains("psql", Qt::CaseInsensitive) ||
        connection_name_.contains("postgres", Qt::CaseInsensitive)) {
      driver_name_upper = "QPSQL";
    } else if (connection_name_.contains("mysql", Qt::CaseInsensitive)) {
      driver_name_upper = "QMYSQL";
    }
    // Add other dialects if needed
  }
  if (driver_name_upper.isEmpty()) {
    // Fallback if connection_name_ doesn't give a hint or isn't
    // checked/available. qWarning() << "QueryBuilder::buildInsertSQLSuffix:
    // Could not reliably determine SQL dialect. Defaulting to MySQL syntax for
    // suffix.";
    driver_name_upper = "QMYSQL";
  }

  if (state_.on_conflict_clause_->action ==
      OnConflictClause::Action::DoNothing) {
    if (driver_name_upper == "QMYSQL") {
      return {QString(),
              QVariantList()}; // MySQL uses INSERT IGNORE, handled by Session
    } else if (driver_name_upper == "QPSQL") {
      sql_suffix_stream << " ON CONFLICT";
      // For PostgreSQL, ON CONFLICT DO NOTHING can be general or targeted.
      // If conflict_target_columns_db_names is set, use it.
      if (!state_.on_conflict_clause_->conflict_target_columns_db_names
               .empty()) {
        sql_suffix_stream << " (";
        for (size_t i = 0; i < state_.on_conflict_clause_
                                   ->conflict_target_columns_db_names.size();
             ++i) {
          sql_suffix_stream << quoteSqlIdentifier(
              state_.on_conflict_clause_->conflict_target_columns_db_names[i]);
          if (i < state_.on_conflict_clause_->conflict_target_columns_db_names
                          .size() -
                      1)
            sql_suffix_stream << ", ";
        }
        sql_suffix_stream << ")";
      }
      // If no specific target columns, it might be ON CONSTRAINT or a general
      // DO NOTHING if the DB implies target from PKs. GORM often requires
      // specifying a target for PG ON CONFLICT. For simplicity here, if no
      // target, assume general (might need specific constraint name in real
      // app).
      sql_suffix_stream << " DO NOTHING";
    }
    // Other dialects might have different syntaxes or no direct "DO NOTHING"
    // via suffix.
  } else if (state_.on_conflict_clause_->action ==
                 OnConflictClause::Action::UpdateAllExcluded ||
             state_.on_conflict_clause_->action ==
                 OnConflictClause::Action::UpdateSpecific) {
    bool first_update_col = true;
    if (driver_name_upper == "QMYSQL") {
      sql_suffix_stream << " ON DUPLICATE KEY UPDATE ";
    } else if (driver_name_upper == "QPSQL") {
      sql_suffix_stream << " ON CONFLICT ";
      // For PG, conflict target is crucial for DO UPDATE.
      // Use specified conflict_target_columns or default to PKs from
      // model_meta.
      std::vector<std::string> pg_conflict_targets;
      if (!state_.on_conflict_clause_->conflict_target_columns_db_names
               .empty()) {
        pg_conflict_targets =
            state_.on_conflict_clause_->conflict_target_columns_db_names;
      } else if (state_.model_meta_ &&
                 !state_.model_meta_->primary_keys_db_names.empty()) {
        pg_conflict_targets = state_.model_meta_->primary_keys_db_names;
      }

      if (!pg_conflict_targets.empty()) {
        sql_suffix_stream << "(";
        for (size_t i = 0; i < pg_conflict_targets.size(); ++i) {
          sql_suffix_stream << quoteSqlIdentifier(pg_conflict_targets[i]);
          if (i < pg_conflict_targets.size() - 1)
            sql_suffix_stream << ", ";
        }
        sql_suffix_stream << ") ";
      } else {
        qWarning("cpporm QueryBuilder: For PostgreSQL ON CONFLICT DO UPDATE, "
                 "conflict target (columns or PK) must be defined.");
        return {QString(), QVariantList()};
      }
      sql_suffix_stream << "DO UPDATE SET ";
    } else {
      // Default to MySQL like behavior or make it an error if dialect unknown
      // for update
      sql_suffix_stream << " ON DUPLICATE KEY UPDATE "; // Fallback
    }

    if (state_.on_conflict_clause_->action ==
        OnConflictClause::Action::UpdateAllExcluded) {
      if (inserted_columns_db_names_for_values_clause.empty() &&
          driver_name_upper !=
              "QPSQL" /*PG can update with empty list using EXCLUDED*/) {
        qWarning("cpporm QueryBuilder: OnConflictUpdateAllExcluded specified "
                 "for non-PG, "
                 "but no columns provided from INSERT part to determine "
                 "VALUES() updates.");
      }
      for (const std::string &db_col_name :
           inserted_columns_db_names_for_values_clause) {
        bool skip_this_column_in_set = false;
        if (state_.model_meta_) {
          for (const auto &pk_name_from_meta :
               state_.model_meta_->primary_keys_db_names) {
            if (pk_name_from_meta == db_col_name) {
              skip_this_column_in_set =
                  true; // Don't update PKs themselves in SET for MySQL
              break;
            }
          }
          if (driver_name_upper ==
              "QPSQL") { // For PG, don't update columns that are part of the
                         // conflict target
            const auto &conflict_targets =
                state_.on_conflict_clause_->conflict_target_columns_db_names
                            .empty() &&
                        state_.model_meta_
                    ? state_.model_meta_->primary_keys_db_names
                    : state_.on_conflict_clause_
                          ->conflict_target_columns_db_names;
            for (const auto &conflict_col : conflict_targets) {
              if (conflict_col == db_col_name) {
                skip_this_column_in_set = true;
                break;
              }
            }
          }
        }
        if (skip_this_column_in_set)
          continue;

        if (!first_update_col)
          sql_suffix_stream << ", ";

        sql_suffix_stream << quoteSqlIdentifier(db_col_name) << " = ";
        if (driver_name_upper == "QMYSQL") {
          sql_suffix_stream << "VALUES(" << quoteSqlIdentifier(db_col_name)
                            << ")";
        } else if (driver_name_upper == "QPSQL") {
          sql_suffix_stream << "EXCLUDED." << quoteSqlIdentifier(db_col_name);
        } else {
          sql_suffix_stream << "?";
          qWarning("QueryBuilder::buildInsertSQLSuffix: UpdateAllExcluded for "
                   "this dialect might need specific value passing or is not "
                   "fully supported by this generic builder.");
        }
        first_update_col = false;
      }
      if (first_update_col &&
          !inserted_columns_db_names_for_values_clause.empty()) {
        qWarning(
            "cpporm QueryBuilder: OnConflictUpdateAllExcluded resulted "
            "in empty SET clause (all conflicting/inserted columns might be "
            "PKs or conflict targets). SQL might be invalid.");
        // For some DBs, an empty SET clause is an error.
        // We return what's built, DB will decide. Or, return empty to signal
        // error. For now, let it pass, but this is a sign of problematic input
        // for some DBs.
      }
    } else if (state_.on_conflict_clause_->action ==
               OnConflictClause::Action::UpdateSpecific) {
      if (state_.on_conflict_clause_->update_assignments.empty()) {
        qWarning("cpporm QueryBuilder: OnConflictUpdateSpecific specified, "
                 "but no update assignments provided.");
      }
      for (const auto &assign_pair :
           state_.on_conflict_clause_->update_assignments) {
        // Ensure the column being set in UpdateSpecific is not part of the
        // conflict target for PG
        if (driver_name_upper == "QPSQL" && state_.model_meta_) {
          const auto &conflict_targets =
              state_.on_conflict_clause_->conflict_target_columns_db_names
                      .empty()
                  ? state_.model_meta_->primary_keys_db_names
                  : state_.on_conflict_clause_
                        ->conflict_target_columns_db_names;
          bool is_conflict_target_col = false;
          for (const auto &ct_col : conflict_targets) {
            if (ct_col == assign_pair.first) {
              is_conflict_target_col = true;
              break;
            }
          }
          if (is_conflict_target_col) {
            qWarning() << "QueryBuilder::buildInsertSQLSuffix (PG): Column '"
                       << QString::fromStdString(assign_pair.first)
                       << "' is part of the conflict target and cannot be in "
                          "the SET clause of ON CONFLICT DO UPDATE. Skipping.";
            continue;
          }
        }

        if (!first_update_col)
          sql_suffix_stream << ", ";
        sql_suffix_stream << quoteSqlIdentifier(assign_pair.first) << " = ";

        if (std::holds_alternative<SubqueryExpression>(assign_pair.second)) {
          sql_suffix_stream
              << QueryBuilder::toQVariant(assign_pair.second,
                                          suffix_bindings_accumulator)
                     .toString()
                     .toStdString();
        } else {
          sql_suffix_stream << "?";
          suffix_bindings_accumulator.append(QueryBuilder::toQVariant(
              assign_pair.second, suffix_bindings_accumulator));
        }
        first_update_col = false;
      }
    }

    if (first_update_col &&
        (state_.on_conflict_clause_->action ==
             OnConflictClause::Action::UpdateAllExcluded ||
         state_.on_conflict_clause_->action ==
             OnConflictClause::Action::UpdateSpecific) &&
        !state_.on_conflict_clause_->update_assignments
             .empty() /* For UpdateSpecific, if assignments were provided but
                         all got skipped */
    ) {
      qWarning("cpporm QueryBuilder::buildInsertSQLSuffix: Resulted in an ON "
               "CONFLICT UPDATE clause with no actual assignments (e.g. all "
               "specific assignments targeted conflict keys for PG). SQL may "
               "be invalid.");
      return {QString(), QVariantList()}; // Return empty string to indicate
                                          // failure to build valid suffix
    }
  }

  return {QString::fromStdString(sql_suffix_stream.str()),
          suffix_bindings_accumulator};
}

} // namespace cpporm