// cpporm/builder_parts/query_builder_sql_update.cpp
#include "cpporm/model_base.h" // For ModelMeta, FieldMeta, FieldFlag (indirectly)
#include "cpporm/query_builder.h"

#include <QDebug>
#include <sstream>
#include <variant>

namespace cpporm {

std::pair<QString, QVariantList> QueryBuilder::buildUpdateSQL(
    const std::map<std::string, QueryValue> &updates) const {

  if (std::holds_alternative<SubquerySource>(state_.from_clause_source_)) {
    qWarning("cpporm QueryBuilder::buildUpdateSQL: UPDATE operation cannot "
             "target a subquery directly.");
    return {QString(), QVariantList()};
  }

  QString table_name_qstr = getFromSourceName(); // Use the helper
  if (table_name_qstr.isEmpty()) {
    qWarning("cpporm QueryBuilder: Table name not set for buildUpdateSQL.");
    return {QString(), QVariantList()};
  }

  if (updates.empty()) {
    qWarning("cpporm QueryBuilder: No update values provided for table %s.",
             table_name_qstr.toStdString().c_str());
    return {QString(), QVariantList()};
  }

  std::ostringstream sql_stream;
  QVariantList bound_params_accumulator;

  sql_stream << "UPDATE " << quoteSqlIdentifier(table_name_qstr.toStdString())
             << " SET ";

  QString driverNameUpper;
  const QString &connName = getConnectionName();
  if (!connName.isEmpty()) {
    if (connName.contains("mysql", Qt::CaseInsensitive) ||
        connName.contains("mariadb", Qt::CaseInsensitive)) {
      driverNameUpper = "QMYSQL";
    } else if (connName.contains("psql", Qt::CaseInsensitive) ||
               connName.contains("postgres", Qt::CaseInsensitive)) {
      driverNameUpper = "QPSQL";
    }
  }

  bool first_set_col = true;
  for (const auto &pair : updates) {
    if (!first_set_col) {
      sql_stream << ", ";
    }
    sql_stream << quoteSqlIdentifier(pair.first) << " = ";

    if (std::holds_alternative<SubqueryExpression>(pair.second)) {
      sql_stream << QueryBuilder::toQVariant(pair.second,
                                             bound_params_accumulator)
                        .toString()
                        .toStdString();
    } else {
      bool use_st_geom_from_text = false;
      if (state_.model_meta_ &&
          (driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB")) {
        const FieldMeta *fm = state_.model_meta_->findFieldByDbName(pair.first);
        if (fm &&
            (fm->db_type_hint == "POINT" || fm->db_type_hint == "GEOMETRY" ||
             fm->db_type_hint == "LINESTRING" ||
             fm->db_type_hint == "POLYGON" ||
             fm->db_type_hint == "MULTIPOINT" ||
             fm->db_type_hint == "MULTILINESTRING" ||
             fm->db_type_hint == "MULTIPOLYGON" ||
             fm->db_type_hint == "GEOMETRYCOLLECTION")) {
          use_st_geom_from_text = true;
        }
      }

      if (use_st_geom_from_text) {
        sql_stream << "ST_GeomFromText(?)";
      } else {
        sql_stream << "?";
      }
      bound_params_accumulator.append(
          QueryBuilder::toQVariant(pair.second, bound_params_accumulator));
    }
    first_set_col = false;
  }

  std::string soft_delete_where_fragment;
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
        soft_delete_where_fragment =
            quoteSqlIdentifier(state_.model_meta_->table_name) + "." +
            quoteSqlIdentifier(deleted_at_field->db_name) + " IS NULL";
      }
    }
  }

  bool where_clause_started_by_builder = true;
  build_condition_logic_internal(sql_stream, bound_params_accumulator,
                                 where_clause_started_by_builder,
                                 soft_delete_where_fragment);

  if (where_clause_started_by_builder && soft_delete_where_fragment.empty() &&
      state_.where_conditions_.empty() && state_.or_conditions_.empty() &&
      state_.not_conditions_.empty()) {
    qWarning() << "cpporm QueryBuilder::buildUpdateSQL: Generating UPDATE "
                  "statement without a WHERE clause for table"
               << table_name_qstr
               << ". This will affect ALL rows if not intended.";
  }

  return {QString::fromStdString(sql_stream.str()), bound_params_accumulator};
}

} // namespace cpporm