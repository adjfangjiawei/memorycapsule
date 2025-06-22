#include <QDateTime>  // For timestamp logic, QVariant types in QueryValue
#include <QDebug>     // qWarning
#include <QMetaType>  // For QVariant to std::any conversion types
#include <QVariant>   // QVariantList, QueryValue can hold QVariant types
#include <algorithm>  // For std::transform in driver name to upper

#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "sqldriver/i_sql_driver.h"  // ISqlDriver for hasFeature
#include "sqldriver/sql_enums.h"     // Feature::LastInsertId, Feature::InsertAndReturnId
#include "sqldriver/sql_query.h"     // SqlQuery
#include "sqldriver/sql_value.h"     // SqlValue

namespace cpporm {

    // Session 的 IQueryExecutor::CreateImpl 实现 (单个模型创建)
    // 返回 std::expected<SqlValue, Error>
    std::expected<cpporm_sqldriver::SqlValue, Error> Session::CreateImpl(const QueryBuilder &qb, ModelBase &model_instance, const OnConflictClause *conflict_options_override) {
        const OnConflictClause *active_conflict_clause = conflict_options_override;
        if (!active_conflict_clause && qb.getOnConflictClause()) {
            active_conflict_clause = qb.getOnConflictClause();
        }
        if (!active_conflict_clause && temp_on_conflict_clause_) {  // Check session's temp clause
            active_conflict_clause = temp_on_conflict_clause_.get();
        }

        bool clear_temp_on_conflict_at_end = (active_conflict_clause == temp_on_conflict_clause_.get() && !conflict_options_override && !qb.getOnConflictClause());

        const ModelMeta *meta_ptr = qb.getModelMeta();
        if (!meta_ptr) {  // If QB doesn't have meta, get from model
            meta_ptr = &(model_instance._getOwnModelMeta());
        }
        if (!meta_ptr || meta_ptr->table_name.empty()) {
            if (clear_temp_on_conflict_at_end) this->clearTempOnConflictClause();
            return std::unexpected(Error(ErrorCode::InvalidConfiguration, "CreateImpl: ModelMeta is not valid or table name is empty."));
        }
        const ModelMeta &meta = *meta_ptr;

        Error hook_err = model_instance.beforeCreate(*this);
        if (hook_err) {
            if (clear_temp_on_conflict_at_end) this->clearTempOnConflictClause();
            return std::unexpected(hook_err);
        }

        this->autoSetTimestamps(model_instance, meta, true);
        // extractModelData 返回 SessionModelDataForWrite，其字段值类型已更新为 SqlValue
        internal::SessionModelDataForWrite data_to_write = this->extractModelData(model_instance, meta, false, true);

        if (data_to_write.fields_to_write.empty() && !data_to_write.has_auto_increment_pk) {
            // 检查是否为纯自增主键模型（例如只有一个自增ID字段）
            bool is_simple_auto_inc_model = data_to_write.has_auto_increment_pk && meta.fields.size() == 1 && meta.getPrimaryField() && has_flag(meta.getPrimaryField()->flags, FieldFlag::AutoIncrement);
            if (!is_simple_auto_inc_model) {
                if (clear_temp_on_conflict_at_end) this->clearTempOnConflictClause();
                return std::unexpected(Error(ErrorCode::MappingError,
                                             "No fields to insert for Create operation "
                                             "and not a simple auto-increment model. Table: " +
                                                 meta.table_name));
            }
        }

        std::vector<std::string> field_names_std_vec;
        std::vector<cpporm_sqldriver::SqlValue> values_to_bind_sqlvalue;
        std::vector<std::string> placeholders_std_vec;
        std::vector<std::string> ordered_db_field_names_vec;

        std::string driverNameStdUpper;
        if (db_handle_.driver()) {
            std::string drv_name_full = db_handle_.driverName();
            std::transform(drv_name_full.begin(), drv_name_full.end(), std::back_inserter(driverNameStdUpper), [](unsigned char c) {
                return std::toupper(c);
            });
        }

        for (const auto &[db_name_std, sql_val] : data_to_write.fields_to_write) {
            ordered_db_field_names_vec.push_back(db_name_std);
            field_names_std_vec.push_back(QueryBuilder::quoteSqlIdentifier(db_name_std));
            values_to_bind_sqlvalue.push_back(sql_val);

            bool placeholder_handled = false;
            if (driverNameStdUpper.find("MYSQL") != std::string::npos || driverNameStdUpper.find("MARIADB") != std::string::npos) {
                const FieldMeta *fm = meta.findFieldByDbName(db_name_std);
                if (fm && (fm->db_type_hint == "POINT" || fm->db_type_hint == "GEOMETRY" || fm->db_type_hint == "LINESTRING" || fm->db_type_hint == "POLYGON" || fm->db_type_hint == "MULTIPOINT" || fm->db_type_hint == "MULTILINESTRING" || fm->db_type_hint == "MULTIPOLYGON" ||
                           fm->db_type_hint == "GEOMETRYCOLLECTION")) {
                    placeholders_std_vec.push_back("ST_GeomFromText(?)");
                    placeholder_handled = true;
                }
            }
            // Add similar for PG (ST_GeomFromEWKT) or SQLite (GeomFromText) if needed
            if (!placeholder_handled) {
                placeholders_std_vec.push_back("?");
            }
        }

        std::string sql_verb = "INSERT";
        if (active_conflict_clause && active_conflict_clause->action == OnConflictClause::Action::DoNothing) {
            if (driverNameStdUpper.find("MYSQL") != std::string::npos || driverNameStdUpper.find("MARIADB") != std::string::npos) {
                sql_verb = "INSERT IGNORE";
            } else if (driverNameStdUpper.find("SQLITE") != std::string::npos) {
                // SQLite can use ON CONFLICT DO NOTHING suffix, or INSERT OR IGNORE verb.
                // If buildInsertSQLSuffix handles "ON CONFLICT DO NOTHING" for SQLite, keep INSERT.
                // If we prefer "INSERT OR IGNORE", change sql_verb here and ensure suffix is not added.
                // For now, assume suffix is preferred if available, so sql_verb remains INSERT.
                // If suffix builder returns empty for SQLite DO NOTHING, then change verb:
                // sql_verb = "INSERT OR IGNORE";
            }
        }

        std::string sql_query_base_std;
        std::ostringstream sql_builder_stream;

        if (!field_names_std_vec.empty()) {
            sql_builder_stream << sql_verb << " INTO " << QueryBuilder::quoteSqlIdentifier(meta.table_name) << " (";
            for (size_t i = 0; i < field_names_std_vec.size(); ++i) {
                sql_builder_stream << field_names_std_vec[i] << (i < field_names_std_vec.size() - 1 ? ", " : "");
            }
            sql_builder_stream << ") VALUES (";
            for (size_t i = 0; i < placeholders_std_vec.size(); ++i) {
                sql_builder_stream << placeholders_std_vec[i] << (i < placeholders_std_vec.size() - 1 ? ", " : "");
            }
            sql_builder_stream << ")";
            sql_query_base_std = sql_builder_stream.str();

        } else if (data_to_write.has_auto_increment_pk) {                // Only auto-inc PK, no other fields
            if (driverNameStdUpper.find("PSQL") != std::string::npos) {  // PostgreSQL
                sql_query_base_std = "INSERT INTO " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " DEFAULT VALUES";
            } else {  // MySQL, SQLite, etc.
                sql_query_base_std = sql_verb + " INTO " + QueryBuilder::quoteSqlIdentifier(meta.table_name) + " () VALUES ()";
            }
            values_to_bind_sqlvalue.clear();  // No values to bind in this specific case
        } else {
            if (clear_temp_on_conflict_at_end) this->clearTempOnConflictClause();
            return std::unexpected(Error(ErrorCode::MappingError, "Cannot construct INSERT: no fields and no auto-inc PK. Table: " + meta.table_name));
        }

        QString sql_on_conflict_suffix_qstr;
        QVariantList suffix_qbindings;

        if (active_conflict_clause) {
            // If sql_verb was changed to INSERT IGNORE/OR IGNORE, we might not want a suffix.
            bool skip_suffix_due_to_verb = (sql_verb != "INSERT" && active_conflict_clause->action == OnConflictClause::Action::DoNothing);
            if (!skip_suffix_due_to_verb) {
                QueryBuilder temp_qb_for_suffix(nullptr, this->connection_name_, &meta);
                temp_qb_for_suffix.getState_().on_conflict_clause_ = std::make_unique<OnConflictClause>(*active_conflict_clause);
                auto suffix_pair = temp_qb_for_suffix.buildInsertSQLSuffix(ordered_db_field_names_vec);
                sql_on_conflict_suffix_qstr = suffix_pair.first;
                suffix_qbindings = suffix_pair.second;
            }
        }

        std::string final_sql_query_std = sql_query_base_std;
        if (!sql_on_conflict_suffix_qstr.isEmpty()) {
            final_sql_query_std += " " + sql_on_conflict_suffix_qstr.toStdString();
        }

        std::vector<cpporm_sqldriver::SqlValue> all_bindings_sqlvalue = values_to_bind_sqlvalue;
        for (const QVariant &qv_suffix_bind : suffix_qbindings) {
            all_bindings_sqlvalue.push_back(Session::queryValueToSqlValue(QueryBuilder::qvariantToQueryValue(qv_suffix_bind)));
        }

        bool driver_can_return_last_id = db_handle_.hasFeature(cpporm_sqldriver::Feature::LastInsertId);
        bool use_returning_clause_feature = db_handle_.hasFeature(cpporm_sqldriver::Feature::InsertAndReturnId);

        bool use_returning_for_this_op = false;
        if (use_returning_clause_feature && data_to_write.has_auto_increment_pk && !data_to_write.auto_increment_pk_name_db.empty() && (!active_conflict_clause || active_conflict_clause->action != OnConflictClause::Action::DoNothing)) {
            // PostgreSQL and SQLite (>=3.35) support RETURNING
            if (driverNameStdUpper.find("PSQL") != std::string::npos || driverNameStdUpper.find("SQLITE") != std::string::npos) {
                use_returning_for_this_op = true;
            }
        }

        if (use_returning_for_this_op) {
            final_sql_query_std += " RETURNING " + QueryBuilder::quoteSqlIdentifier(data_to_write.auto_increment_pk_name_db);
        }

        auto [sql_query_obj, exec_err] = execute_query_internal(this->db_handle_, final_sql_query_std, all_bindings_sqlvalue);

        if (clear_temp_on_conflict_at_end) this->clearTempOnConflictClause();

        if (exec_err) return std::unexpected(exec_err);

        long long rows_affected = sql_query_obj.numRowsAffected();
        model_instance._is_persisted = (rows_affected > 0 || (active_conflict_clause && active_conflict_clause->action != OnConflictClause::Action::DoNothing && rows_affected >= 0));

        cpporm_sqldriver::SqlValue returned_id_sv;

        bool was_pure_insert_action = (sql_verb == "INSERT" && !active_conflict_clause);  // True insert without conflict clause
        bool was_insert_ignore_action = (sql_verb == "INSERT IGNORE" || sql_verb == "INSERT OR IGNORE");
        bool was_upsert_action = (active_conflict_clause && active_conflict_clause->action != OnConflictClause::Action::DoNothing);

        if (use_returning_for_this_op && (was_pure_insert_action || was_upsert_action) && rows_affected > 0) {
            if (sql_query_obj.next()) returned_id_sv = sql_query_obj.value(0);
        } else if (data_to_write.has_auto_increment_pk && driver_can_return_last_id && (was_pure_insert_action || was_insert_ignore_action) && rows_affected == 1) {
            returned_id_sv = sql_query_obj.lastInsertId();
        } else if (data_to_write.has_auto_increment_pk && driver_can_return_last_id && was_upsert_action && rows_affected > 0) {
            // MySQL ON DUPLICATE KEY UPDATE: rows_affected=1 for INSERT, 2 for UPDATE. lastInsertId() is new ID for INSERT.
            if ((driverNameStdUpper.find("MYSQL") != std::string::npos || driverNameStdUpper.find("MARIADB") != std::string::npos) && rows_affected == 1) {
                returned_id_sv = sql_query_obj.lastInsertId();
            }
            // SQLite ON CONFLICT DO UPDATE: lastInsertId() is rowid of updated/inserted row.
            else if (driverNameStdUpper.find("SQLITE") != std::string::npos) {
                returned_id_sv = sql_query_obj.lastInsertId();
            }
        }

        if (returned_id_sv.isValid() && !returned_id_sv.isNull() && data_to_write.has_auto_increment_pk) {
            std::any pk_val_any;
            bool conversion_ok = false;
            const auto &pk_type = data_to_write.pk_cpp_type_for_autoincrement;
            const std::string &pk_cpp_name = data_to_write.pk_cpp_name_for_autoincrement;

            // Convert SqlValue to std::any. mapRowToModel has similar logic.
            if (pk_type == typeid(int))
                pk_val_any = returned_id_sv.toInt32(&conversion_ok);
            else if (pk_type == typeid(long long))
                pk_val_any = returned_id_sv.toInt64(&conversion_ok);
            else if (pk_type == typeid(unsigned int))
                pk_val_any = returned_id_sv.toUInt32(&conversion_ok);
            else if (pk_type == typeid(unsigned long long))
                pk_val_any = returned_id_sv.toUInt64(&conversion_ok);
            else if (pk_type == typeid(std::string))
                pk_val_any = returned_id_sv.toString(&conversion_ok);
            // Add other types if PK can be them (e.g. UUID as string)
            else {
                qWarning() << "CreateImpl: PK backfill for type " << pk_type.name() << " is not directly supported. Attempting string conversion.";
                pk_val_any = returned_id_sv.toString(&conversion_ok);
            }

            if (conversion_ok) {
                Error set_pk_err = model_instance.setFieldValue(pk_cpp_name, pk_val_any);
                if (set_pk_err) qWarning() << "CreateImpl: Error setting auto-incremented PK '" << QString::fromStdString(pk_cpp_name) << "': " << QString::fromStdString(set_pk_err.toString());
            } else {
                std::string sv_str_val = returned_id_sv.toString();
                qWarning() << "CreateImpl: Conversion failed for PK backfill. DB val (SqlValue):" << QString::fromStdString(sv_str_val) << " (type: " << returned_id_sv.typeName() << ") to C++ type " << pk_type.name();
            }
        }

        if (model_instance._is_persisted) {  // Only call afterCreate if model is actually persisted
            hook_err = model_instance.afterCreate(*this);
            if (hook_err) return std::unexpected(hook_err);
        }

        if (returned_id_sv.isValid() && !returned_id_sv.isNull()) return returned_id_sv;
        return cpporm_sqldriver::SqlValue(static_cast<int64_t>(rows_affected));
    }

    std::expected<cpporm_sqldriver::SqlValue, Error> Session::Create(ModelBase &model, const OnConflictClause *conflict_options_override) {
        QueryBuilder qb = this->Model(&model);
        return this->CreateImpl(qb, model, conflict_options_override);
    }

}  // namespace cpporm