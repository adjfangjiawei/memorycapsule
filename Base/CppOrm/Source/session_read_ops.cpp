#include <QDebug>     // qWarning, qInfo
#include <QVariant>   // QVariantList for QueryBuilder, and conversion helpers
#include <algorithm>  // For std::transform etc.

#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"               // 主头文件
#include "cpporm_sqldriver/sql_query.h"   // SqlQuery
#include "cpporm_sqldriver/sql_record.h"  // SqlRecord
#include "cpporm_sqldriver/sql_value.h"   // SqlValue

namespace cpporm {

    Error Session::FirstImpl(const QueryBuilder &qb, ModelBase &result_model) {
        const ModelMeta *meta = qb.getModelMeta();
        if (!meta) {
            // 如果 QB 没有 meta，尝试从 result_model 获取
            meta = &(result_model._getOwnModelMeta());
            if (!meta || meta->table_name.empty()) {  // 检查从模型获取的 meta 是否有效
                return Error(ErrorCode::InvalidConfiguration, "FirstImpl: Could not determine ModelMeta for query.");
            }
        }

        QueryBuilder local_qb = qb;                                   // 创建副本以修改
        if (local_qb.getModelMeta() == nullptr && meta != nullptr) {  // 如果副本 QB 没有 meta，但我们有，则设置它
            local_qb.Model(*meta);
        }
        local_qb.Limit(1);

        auto [sql_qstr, params_qvariantlist] = local_qb.buildSelectSQL();
        std::string sql_std_str = sql_qstr.toStdString();

        if (sql_std_str.empty()) {
            return Error(ErrorCode::StatementPreparationError, "Failed to build SQL for First operation.");
        }

        std::vector<cpporm_sqldriver::SqlValue> params_sqlvalue;
        params_sqlvalue.reserve(params_qvariantlist.size());
        for (const QVariant &qv : params_qvariantlist) {
            params_sqlvalue.push_back(Session::queryValueToSqlValue(QueryBuilder::qvariantToQueryValue(qv)));
        }

        auto [sql_query_obj, exec_err] = execute_query_internal(this->db_handle_, sql_std_str, params_sqlvalue);

        if (exec_err) {
            return exec_err;
        }

        if (sql_query_obj.next()) {
            Error map_err = mapRowToModel(sql_query_obj, result_model, *meta);
            if (map_err) {
                qWarning() << "cpporm Session::FirstImpl: Error mapping row:" << QString::fromStdString(map_err.toString());
                return map_err;
            }
            // result_model._is_persisted = true; // mapRowToModel 应该设置这个
            Error hook_err = result_model.afterFind(*this);
            if (hook_err) return hook_err;

            if (!qb.getPreloadRequests().empty()) {
                std::vector<ModelBase *> models_for_preload = {&result_model};
                Error preload_err = this->processPreloadsInternal(qb, models_for_preload);
                if (preload_err) {
                    qWarning() << "Session::FirstImpl: Preloading failed after fetching model: " << QString::fromStdString(preload_err.toString());
                    // return preload_err; // Decide if this is fatal
                }
            }
            return make_ok();
        } else {
            return Error(ErrorCode::RecordNotFound, "No record found for First operation.");
        }
    }

    Error Session::FindImpl(const QueryBuilder &qb, std::vector<std::unique_ptr<ModelBase>> &results_vector, std::function<std::unique_ptr<ModelBase>()> element_type_factory) {
        if (!element_type_factory) {
            return Error(ErrorCode::InternalError, "Element type factory function is null for Find operation.");
        }

        const ModelMeta *meta_for_query = qb.getModelMeta();
        QueryBuilder local_qb = qb;  // 创建副本以可能修改

        if (!meta_for_query) {
            auto temp_instance = element_type_factory();
            if (temp_instance && !temp_instance->_getOwnModelMeta().table_name.empty()) {
                meta_for_query = &(temp_instance->_getOwnModelMeta());
                if (local_qb.getModelMeta() == nullptr) {  // 如果副本 QB 没有 meta，则设置它
                    local_qb.Model(*meta_for_query);
                }
            } else {
                return Error(ErrorCode::InvalidConfiguration,
                             "FindImpl: Could not determine ModelMeta for query from "
                             "QueryBuilder or factory.");
            }
        }

        auto [sql_qstr, params_qvariantlist] = local_qb.buildSelectSQL();
        std::string sql_std_str = sql_qstr.toStdString();

        if (sql_std_str.empty()) {
            return Error(ErrorCode::StatementPreparationError, "Failed to build SQL for Find operation.");
        }

        std::vector<cpporm_sqldriver::SqlValue> params_sqlvalue;
        params_sqlvalue.reserve(params_qvariantlist.size());
        for (const QVariant &qv : params_qvariantlist) {
            params_sqlvalue.push_back(Session::queryValueToSqlValue(QueryBuilder::qvariantToQueryValue(qv)));
        }

        auto [sql_query_obj, exec_err] = execute_query_internal(this->db_handle_, sql_std_str, params_sqlvalue);
        if (exec_err) {
            return exec_err;
        }

        results_vector.clear();
        while (sql_query_obj.next()) {
            std::unique_ptr<ModelBase> new_element = element_type_factory();
            if (!new_element) {
                return Error(ErrorCode::InternalError, "Element factory returned nullptr inside Find loop.");
            }
            Error map_err = mapRowToModel(sql_query_obj, *new_element, *meta_for_query);
            if (map_err) {
                qWarning() << "cpporm Session::FindImpl: Error mapping row: " << QString::fromStdString(map_err.toString()) << ". SQL was: " << QString::fromStdString(sql_std_str);
                // return map_err; // 通常不应因单行映射失败而中止整个查找
                continue;  // 跳过此行
            }
            // new_element->_is_persisted = true; // mapRowToModel 应该设置这个
            Error hook_err = new_element->afterFind(*this);
            if (hook_err) {
                qWarning() << "cpporm Session::FindImpl: afterFind hook failed for an element: " << QString::fromStdString(hook_err.toString());
                // return hook_err; // Decide if this is fatal
            }
            results_vector.push_back(std::move(new_element));
        }

        if (!results_vector.empty() && !qb.getPreloadRequests().empty()) {
            Error preload_err = this->processPreloads(qb, results_vector);
            if (preload_err) {
                qWarning() << "cpporm Session::FindImpl: Preloading failed: " << QString::fromStdString(preload_err.toString());
                // return preload_err; // Decide if this is fatal
            }
        }
        return make_ok();
    }

    std::expected<int64_t, Error> Session::CountImpl(const QueryBuilder &qb_const) {
        QueryBuilder qb = qb_const;  // 创建副本
        // 如果 QB 没有 ModelMeta，尝试从一个临时模型实例推断（如果可能，但不直接可行）
        // Count 通常需要知道 FROM 子句。如果 QB 没设置 Model/Table，buildSelectSQL 会失败。
        if (!qb.getModelMeta() && qb.getFromSourceName().isEmpty()) {
            return std::unexpected(Error(ErrorCode::InvalidConfiguration, "CountImpl: QueryBuilder has no Model or Table set."));
        }

        if (!qb.getGroupClause().empty()) {
            qWarning(
                "cpporm Session::CountImpl: Count() called with existing GROUP "
                "BY clause. Clearing GROUP BY for total count.");
            qb.Group("");
        }
        qb.Select("COUNT(*)");
        qb.Order("");
        qb.Limit(-1);
        qb.Offset(-1);
        if (!qb.getState_().preload_requests_.empty()) {                                    // 直接访问 state_ 来修改
            QueryBuilderState &mutable_state = const_cast<QueryBuilder &>(qb).getState_();  // 需要 const_cast 来修改副本的状态
            mutable_state.preload_requests_.clear();
        }

        auto [sql_qstr, params_qvariantlist] = qb.buildSelectSQL();
        std::string sql_std_str = sql_qstr.toStdString();

        if (sql_std_str.empty()) {
            return std::unexpected(Error(ErrorCode::StatementPreparationError, "Failed to build SQL for Count operation."));
        }

        std::vector<cpporm_sqldriver::SqlValue> params_sqlvalue;
        params_sqlvalue.reserve(params_qvariantlist.size());
        for (const QVariant &qv : params_qvariantlist) {
            params_sqlvalue.push_back(Session::queryValueToSqlValue(QueryBuilder::qvariantToQueryValue(qv)));
        }

        auto [sql_query_obj, err] = execute_query_internal(this->db_handle_, sql_std_str, params_sqlvalue);
        if (err) {
            return std::unexpected(err);
        }

        if (sql_query_obj.next()) {
            bool ok_conversion;
            cpporm_sqldriver::SqlValue count_sv = sql_query_obj.value(0);
            int64_t count_val = count_sv.toInt64(&ok_conversion);
            if (ok_conversion) {
                return count_val;
            } else {
                std::string sv_str_val = count_sv.toString();
                return std::unexpected(Error(ErrorCode::MappingError, "Failed to convert COUNT(*) result to integer. Value: " + sv_str_val));
            }
        } else {
            qWarning() << "cpporm Session::CountImpl: COUNT(*) query returned no rows "
                          "(unexpected). SQL:"
                       << QString::fromStdString(sql_std_str);
            return std::unexpected(Error(ErrorCode::QueryExecutionError, "COUNT(*) query returned no rows."));
        }
    }

}  // namespace cpporm