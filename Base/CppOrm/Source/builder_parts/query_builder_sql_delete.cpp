// cpporm/builder_parts/query_builder_sql_delete.cpp
#include "cpporm/model_base.h" // For ModelMeta, FieldMeta, FieldFlag
#include "cpporm/query_builder.h"

#include <QDebug>
#include <sstream>
#include <variant> // For std::holds_alternative

namespace cpporm {

std::pair<QString, QVariantList> QueryBuilder::buildDeleteSQL() const {
  if (std::holds_alternative<SubquerySource>(state_.from_clause_source_)) {
    qWarning("cpporm QueryBuilder::buildDeleteSQL: DELETE operation cannot "
             "target a subquery directly.");
    return {QString(), QVariantList()};
  }

  QString table_name_qstr = getFromSourceName(); // Use the helper
  if (table_name_qstr.isEmpty()) {
    qWarning("cpporm QueryBuilder: Table name not set for buildDeleteSQL.");
    return {QString(), QVariantList()};
  }

  std::ostringstream sql_stream;
  QVariantList bound_params_accumulator;
  sql_stream << "DELETE FROM "
             << quoteSqlIdentifier(table_name_qstr.toStdString());

  std::string soft_delete_target_fragment_for_hard_delete;
  if (state_.model_meta_ && state_.apply_soft_delete_scope_) {
    // Check if the FROM source string matches the model's table name
    // or if FROM source string is empty (implying model's table)
    bool apply_sd_on_this_from_source = false;
    if (std::holds_alternative<std::string>(state_.from_clause_source_)) {
      const std::string &from_name_str =
          std::get<std::string>(state_.from_clause_source_);
      if ((!from_name_str.empty() &&
           from_name_str == state_.model_meta_->table_name) ||
          (from_name_str.empty() && !state_.model_meta_->table_name.empty())) {
        apply_sd_on_this_from_source = true;
      }
    }
    if (apply_sd_on_this_from_source) {
      if (const FieldMeta *deleted_at_field =
              state_.model_meta_->findFieldWithFlag(FieldFlag::DeletedAt)) {
        soft_delete_target_fragment_for_hard_delete =
            quoteSqlIdentifier(state_.model_meta_->table_name) + "." +
            quoteSqlIdentifier(deleted_at_field->db_name) + " IS NULL";
      }
    }
  }

  bool where_clause_started_by_builder = true;
  build_condition_logic_internal(sql_stream, bound_params_accumulator,
                                 where_clause_started_by_builder,
                                 soft_delete_target_fragment_for_hard_delete);

  if (where_clause_started_by_builder &&
      soft_delete_target_fragment_for_hard_delete.empty() &&
      state_.where_conditions_.empty() && state_.or_conditions_.empty() &&
      state_.not_conditions_.empty()) {
    qWarning() << "cpporm QueryBuilder::buildDeleteSQL: Generating DELETE "
                  "statement without a WHERE clause for table"
               << table_name_qstr
               << ". This will affect ALL rows if not intended.";
  }
  return {QString::fromStdString(sql_stream.str()), bound_params_accumulator};
}

} // namespace cpporm