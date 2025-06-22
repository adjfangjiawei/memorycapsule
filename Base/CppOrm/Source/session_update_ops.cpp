#include <QDateTime>  // For timestamp logic, QVariant types in QueryValue
#include <QDebug>     // qInfo, qWarning
#include <QVariant>   // QVariantList from QueryBuilder
#include <algorithm>  // For std::transform

#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm_sqldriver/sql_query.h"  // SqlQuery
#include "cpporm_sqldriver/sql_value.h"  // SqlValue

namespace cpporm {

    std::expected<long long, Error> Session::UpdatesImpl(const QueryBuilder &qb_const, const std::map<std::string, QueryValue> &updates_map_in) {
        QueryBuilder qb = qb_const;  // Work with a copy

        if (updates_map_in.empty()) {
            qInfo("cpporm Session::UpdatesImpl: No update values provided.");
            return 0LL;
        }

        std::map<std::string, QueryValue> final_updates = updates_map_in;
        const ModelMeta *meta = qb.getModelMeta();

        if (meta) {
            bool update_model_table_directly = false;
            if (std::holds_alternative<std::string>(qb.getFromClauseSource())) {
                const std::string &from_name = std::get<std::string>(qb.getFromClauseSource());
                if ((!from_name.empty() && from_name == meta->table_name) || (from_name.empty() && !meta->table_name.empty())) {
                    update_model_table_directly = true;
                }
            }

            if (update_model_table_directly) {
                if (const FieldMeta *updatedAtField = meta->findFieldWithFlag(FieldFlag::UpdatedAt)) {
                    if (updatedAtField->cpp_type == typeid(QDateTime)) {
                        final_updates[updatedAtField->db_name] = QDateTime::currentDateTimeUtc();
                    }
                }
            }
        }

        auto [sql_qstr, params_qvariantlist] = qb.buildUpdateSQL(final_updates);
        std::string sql_std_str = sql_qstr.toStdString();

        if (sql_std_str.empty()) {
            return std::unexpected(Error(ErrorCode::StatementPreparationError,
                                         "Failed to build SQL for Updates operation. Target might be "
                                         "invalid or table name missing."));
        }

        std::vector<cpporm_sqldriver::SqlValue> params_sqlvalue;
        params_sqlvalue.reserve(params_qvariantlist.size());
        for (const QVariant &qv : params_qvariantlist) {
            params_sqlvalue.push_back(Session::queryValueToSqlValue(QueryBuilder::qvariantToQueryValue(qv)));
        }

        auto [sql_query_obj, exec_err] = execute_query_internal(this->db_handle_, sql_std_str, params_sqlvalue);
        if (exec_err) {
            return std::unexpected(exec_err);
        }

        return sql_query_obj.numRowsAffected();
    }

    std::expected<long long, Error> Session::Updates(QueryBuilder qb, const std::map<std::string, QueryValue> &updates) {
        if (qb.getExecutor() != this && qb.getExecutor() != nullptr) {
            qWarning(
                "Session::Updates(QueryBuilder, ...): QueryBuilder was associated "
                "with a different executor. The operation will use THIS session's context "
                "by calling its UpdatesImpl. Ensure this is intended.");
        }
        // Always call this session's Impl to ensure correct context and timestamp handling
        return this->UpdatesImpl(qb, updates);
    }

    std::expected<long long, Error> Session::Updates(const ModelMeta &meta, const std::map<std::string, QueryValue> &updates_map, const std::map<std::string, QueryValue> &conditions) {
        if (updates_map.empty()) {
            qInfo("cpporm Session::Updates (by meta): No update values provided.");
            return 0LL;
        }
        QueryBuilder qb = this->Model(meta);
        if (!conditions.empty()) {
            qb.Where(conditions);
        }
        return this->UpdatesImpl(qb, updates_map);
    }

    std::expected<long long, Error> Session::Updates(const ModelBase &model_condition, const std::map<std::string, QueryValue> &updates_map) {
        if (updates_map.empty()) {
            qInfo("cpporm Session::Updates (by model): No update values provided.");
            return 0LL;
        }
        const ModelMeta &meta = model_condition._getOwnModelMeta();
        QueryBuilder qb = this->Model(meta);

        if (meta.primary_keys_db_names.empty()) {
            return std::unexpected(Error(ErrorCode::MappingError, "Updates by model instance: No primary key defined for model " + meta.table_name));
        }

        std::map<std::string, QueryValue> pk_conditions;
        for (const auto &pk_db_name : meta.primary_keys_db_names) {
            const FieldMeta *pk_field = meta.findFieldByDbName(pk_db_name);
            if (!pk_field) {
                return std::unexpected(Error(ErrorCode::InternalError, "Updates by model instance: PK field meta not found for " + pk_db_name));
            }
            std::any pk_val_any = model_condition.getFieldValue(pk_field->cpp_name);
            if (!pk_val_any.has_value()) {
                return std::unexpected(Error(ErrorCode::MappingError, "Updates by model instance: PK value for " + pk_db_name + " is not set in the model."));
            }
            QueryValue qv_pk = Session::anyToQueryValueForSessionConvenience(pk_val_any);
            if (std::holds_alternative<std::nullptr_t>(qv_pk) && pk_val_any.has_value()) {
                return std::unexpected(Error(ErrorCode::MappingError, "Updates by model_condition: Unsupported PK type (" + std::string(pk_val_any.type().name()) + ") for field " + pk_db_name));
            }
            pk_conditions[pk_db_name] = qv_pk;
        }

        if (pk_conditions.empty()) {  // Should be caught by !pk_val_any.has_value() or conversion failure
            return std::unexpected(Error(ErrorCode::MappingError, "Updates by model instance: Failed to extract valid PK conditions."));
        }
        qb.Where(pk_conditions);
        return this->UpdatesImpl(qb, updates_map);
    }

}  // namespace cpporm