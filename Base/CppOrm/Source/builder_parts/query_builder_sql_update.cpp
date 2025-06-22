// cpporm/builder_parts/query_builder_sql_update.cpp
#include <QDebug>     // For qWarning
#include <algorithm>  // For std::tolower in string comparison
#include <sstream>    // For std::ostringstream
#include <variant>    // For std::holds_alternative

#include "cpporm/model_base.h"  // For ModelMeta, FieldMeta, FieldFlag (indirectly for ST_GeomFromText logic)
#include "cpporm/query_builder.h"

namespace cpporm {

    // Helper (already defined in query_builder_sql_select.cpp, ideally should be in a common util or QueryBuilder itself if static)
    // bool string_contains_ci(const std::string& text, const std::string& pattern); // Declaration

    std::pair<QString, QVariantList> QueryBuilder::buildUpdateSQL(const std::map<std::string, QueryValue> &updates) const {
        if (std::holds_alternative<SubquerySource>(state_.from_clause_source_)) {
            qWarning(
                "cpporm QueryBuilder::buildUpdateSQL: UPDATE operation cannot "
                "target a subquery directly.");
            return {QString(), QVariantList()};
        }

        QString table_name_qstr = getFromSourceName();  // Returns QString
        if (table_name_qstr.isEmpty()) {
            qWarning("cpporm QueryBuilder: Table name not set for buildUpdateSQL.");
            return {QString(), QVariantList()};
        }

        if (updates.empty()) {
            qWarning("cpporm QueryBuilder: No update values provided for table %s.", table_name_qstr.toStdString().c_str());
            return {QString(), QVariantList()};
        }

        std::ostringstream sql_stream;
        QVariantList bound_params_accumulator;

        sql_stream << "UPDATE " << quoteSqlIdentifier(table_name_qstr.toStdString()) << " SET ";

        std::string driver_name_upper_std;
        const std::string &conn_name_std = getConnectionName();  // std::string

        if (string_contains_ci(conn_name_std, "mysql") || string_contains_ci(conn_name_std, "mariadb")) {
            driver_name_upper_std = "QMYSQL";
        } else if (string_contains_ci(conn_name_std, "psql") || string_contains_ci(conn_name_std, "postgres")) {
            driver_name_upper_std = "QPSQL";
        } else if (string_contains_ci(conn_name_std, "sqlite")) {
            driver_name_upper_std = "QSQLITE";
        }
        // driver_name_upper_std might remain empty if no match

        bool first_set_col = true;
        for (const auto &pair : updates) {
            if (!first_set_col) {
                sql_stream << ", ";
            }
            sql_stream << quoteSqlIdentifier(pair.first) << " = ";

            if (std::holds_alternative<SubqueryExpression>(pair.second)) {
                // toQVariant is static, appends subquery bindings to bound_params_accumulator
                sql_stream << QueryBuilder::toQVariant(pair.second, bound_params_accumulator).toString().toStdString();  // Injects "(subquery_sql)"
            } else {
                bool use_st_geom_from_text = false;
                if (state_.model_meta_ && (driver_name_upper_std == "QMYSQL" /*|| driver_name_upper_std == "QMARIADB" - covered by QMYSQL logic*/)) {
                    const FieldMeta *fm = state_.model_meta_->findFieldByDbName(pair.first);
                    if (fm && (fm->db_type_hint == "POINT" || fm->db_type_hint == "GEOMETRY" || fm->db_type_hint == "LINESTRING" || fm->db_type_hint == "POLYGON" || fm->db_type_hint == "MULTIPOINT" || fm->db_type_hint == "MULTILINESTRING" || fm->db_type_hint == "MULTIPOLYGON" ||
                               fm->db_type_hint == "GEOMETRYCOLLECTION")) {
                        use_st_geom_from_text = true;
                    }
                }
                // Similar logic for PostgreSQL with ST_GeomFromEWKT or ::geometry might be needed
                // if PostGIS is used and types are WKT strings.
                // For SQLite with SpatiaLite, it would be GeomFromText() or similar.

                if (use_st_geom_from_text) {
                    sql_stream << "ST_GeomFromText(?)";
                } else {
                    sql_stream << "?";
                }
                // Regular value, convert to QVariant and add to accumulator
                // toQVariant here does not add to accumulator, it just converts.
                bound_params_accumulator.append(QueryBuilder::toQVariant(pair.second, bound_params_accumulator));
            }
            first_set_col = false;
        }

        std::string soft_delete_where_fragment;
        if (state_.model_meta_ && state_.apply_soft_delete_scope_) {
            bool apply_sd_on_this_from_source = false;
            if (std::holds_alternative<std::string>(state_.from_clause_source_)) {
                const std::string &from_name_str = std::get<std::string>(state_.from_clause_source_);
                if ((!from_name_str.empty() && from_name_str == state_.model_meta_->table_name) || (from_name_str.empty() && !state_.model_meta_->table_name.empty())) {
                    apply_sd_on_this_from_source = true;
                }
            }
            if (apply_sd_on_this_from_source) {
                if (const FieldMeta *deleted_at_field = state_.model_meta_->findFieldWithFlag(FieldFlag::DeletedAt)) {
                    soft_delete_where_fragment = quoteSqlIdentifier(state_.model_meta_->table_name) + "." + quoteSqlIdentifier(deleted_at_field->db_name) + " IS NULL";
                }
            }
        }

        bool where_clause_started_by_builder = true;  // True if "WHERE" needs to be written by build_condition_logic_internal
        build_condition_logic_internal(sql_stream, bound_params_accumulator, where_clause_started_by_builder, soft_delete_where_fragment);

        if (where_clause_started_by_builder && soft_delete_where_fragment.empty() && state_.where_conditions_.empty() && state_.or_conditions_.empty() && state_.not_conditions_.empty()) {
            qWarning() << "cpporm QueryBuilder::buildUpdateSQL: Generating UPDATE "
                          "statement without a WHERE clause for table "
                       << table_name_qstr  // This is still QString
                       << ". This will affect ALL rows if not intended.";
        }

        return {QString::fromStdString(sql_stream.str()), bound_params_accumulator};
    }

}  // namespace cpporm