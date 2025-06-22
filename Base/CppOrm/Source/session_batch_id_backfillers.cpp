// Base/CppOrm/Source/session_batch_id_backfillers.cpp
#include <QDebug>
#include <QVariant>
#include <algorithm>

#include "cpporm/builder_parts/query_builder_state.h"  // For OnConflictClause
#include "cpporm/model_base.h"
#include "cpporm/session.h"
#include "cpporm/session_priv_batch_helpers.h"
#include "cpporm_sqldriver/sql_database.h"
#include "cpporm_sqldriver/sql_enums.h"
#include "cpporm_sqldriver/sql_query.h"
#include "cpporm_sqldriver/sql_value.h"

namespace cpporm {
    namespace internal_batch_helpers {

        std::vector<ModelBase *> backfillIdsFromReturning(cpporm_sqldriver::SqlQuery &executed_query, const ModelMeta &meta, const std::vector<ModelBase *> &models_to_backfill_from, const std::string &pk_cpp_name_str, const std::type_index &pk_cpp_type) {
            std::vector<ModelBase *> successfully_backfilled_models;
            if (models_to_backfill_from.empty()) {
                return successfully_backfilled_models;
            }

            for (ModelBase *model_to_backfill : models_to_backfill_from) {
                if (!model_to_backfill || !model_to_backfill->_is_persisted) {
                    continue;
                }

                if (!executed_query.next()) {
                    qWarning() << "backfillIdsFromReturning: RETURNING clause provided fewer "
                                  "ID rows than the number of persisted models in the batch for table "
                               << QString::fromStdString(meta.table_name);
                    break;
                }
                cpporm_sqldriver::SqlValue id_sql_val_ret = executed_query.value(0);
                std::any pk_any_val;
                bool conv_ok = false;

                if (id_sql_val_ret.isNull()) {
                    conv_ok = true;
                } else {
                    if (pk_cpp_type == typeid(int))
                        pk_any_val = id_sql_val_ret.toInt32(&conv_ok);
                    else if (pk_cpp_type == typeid(long long))
                        pk_any_val = id_sql_val_ret.toInt64(&conv_ok);
                    else if (pk_cpp_type == typeid(unsigned int))
                        pk_any_val = id_sql_val_ret.toUInt32(&conv_ok);
                    else if (pk_cpp_type == typeid(unsigned long long))
                        pk_any_val = id_sql_val_ret.toUInt64(&conv_ok);
                    else if (pk_cpp_type == typeid(std::string))
                        pk_any_val = id_sql_val_ret.toString(&conv_ok);
                    else if (pk_cpp_type == typeid(QByteArray))  // Handle QByteArray for UUID etc.
                        pk_any_val = id_sql_val_ret.toByteArray(&conv_ok);
                    else {
                        qWarning() << "backfillIdsFromReturning: Unsupported C++ PK type for backfill: " << pk_cpp_type.name() << "for table" << QString::fromStdString(meta.table_name) << ". Attempting string conversion.";
                        pk_any_val = id_sql_val_ret.toString(&conv_ok);
                    }
                }

                if (conv_ok) {
                    Error set_err = model_to_backfill->setFieldValue(pk_cpp_name_str, pk_any_val);
                    if (set_err) {
                        qWarning() << "backfillIdsFromReturning: Error setting PK value for table " << QString::fromStdString(meta.table_name) << ", field " << QString::fromStdString(pk_cpp_name_str) << " after RETURNING:" << QString::fromStdString(set_err.toString());
                    } else {
                        successfully_backfilled_models.push_back(model_to_backfill);
                    }
                } else {
                    qWarning() << "backfillIdsFromReturning: PK backfill conversion failed for RETURNING. SqlValue type:" << id_sql_val_ret.typeName() << "to C++ type" << pk_cpp_type.name() << " for table " << QString::fromStdString(meta.table_name);
                }
            }
            return successfully_backfilled_models;
        }

        std::vector<ModelBase *> backfillIdsFromLastInsertId(cpporm_sqldriver::SqlQuery &executed_query,
                                                             const Session &session,
                                                             const ModelMeta &meta,
                                                             const std::vector<ModelBase *> &models_to_backfill_from,
                                                             long long total_rows_affected_by_query,
                                                             const std::string &pk_cpp_name_str,
                                                             const std::type_index &pk_cpp_type,
                                                             const OnConflictClause *active_conflict_clause) {
            std::vector<ModelBase *> successfully_backfilled_models;
            if (models_to_backfill_from.empty() || total_rows_affected_by_query <= 0) {
                return successfully_backfilled_models;
            }

            cpporm_sqldriver::SqlValue first_id_sql_val = executed_query.lastInsertId();
            if (!first_id_sql_val.isValid() || first_id_sql_val.isNull()) {
                bool is_zero_id = false;
                bool conv_check_ok = false;
                if (first_id_sql_val.type() == cpporm_sqldriver::SqlValueType::Int32 && first_id_sql_val.toInt32(&conv_check_ok) == 0 && conv_check_ok)
                    is_zero_id = true;
                else if (first_id_sql_val.type() == cpporm_sqldriver::SqlValueType::Int64 && first_id_sql_val.toInt64(&conv_check_ok) == 0 && conv_check_ok)
                    is_zero_id = true;

                if (!is_zero_id || (is_zero_id && pk_cpp_type != typeid(int) && pk_cpp_type != typeid(long long))) {
                    qWarning() << "backfillIdsFromLastInsertId: lastInsertId is invalid, null, or zero (and PK is not int/longlong) for table " << QString::fromStdString(meta.table_name) << ". Value: " << QString::fromStdString(first_id_sql_val.toString());
                    return successfully_backfilled_models;
                }
            }

            std::string db_driver_name_upper_std;
            // Use a const reference to the db_handle to call const methods
            const cpporm_sqldriver::SqlDatabase &const_db_handle = session.getDbHandle();
            if (const_db_handle.driver()) {
                std::string drv_name_full = const_db_handle.driverName();
                std::transform(drv_name_full.begin(), drv_name_full.end(), std::back_inserter(db_driver_name_upper_std), [](unsigned char c) {
                    return std::toupper(c);
                });
            }

            if (models_to_backfill_from.size() == 1 && total_rows_affected_by_query >= 1) {
                ModelBase *single_model = nullptr;
                for (ModelBase *m : models_to_backfill_from) {
                    if (m && m->_is_persisted) {
                        single_model = m;
                        break;
                    }
                }

                if (single_model) {
                    std::any pk_any_val;
                    bool conv_ok = false;
                    if (pk_cpp_type == typeid(int))
                        pk_any_val = first_id_sql_val.toInt32(&conv_ok);
                    else if (pk_cpp_type == typeid(long long))
                        pk_any_val = first_id_sql_val.toInt64(&conv_ok);
                    else if (pk_cpp_type == typeid(unsigned int))
                        pk_any_val = first_id_sql_val.toUInt32(&conv_ok);
                    else if (pk_cpp_type == typeid(unsigned long long))
                        pk_any_val = first_id_sql_val.toUInt64(&conv_ok);
                    else if (pk_cpp_type == typeid(std::string))
                        pk_any_val = first_id_sql_val.toString(&conv_ok);
                    else if (pk_cpp_type == typeid(QByteArray))
                        pk_any_val = first_id_sql_val.toByteArray(&conv_ok);
                    else {
                        pk_any_val = first_id_sql_val.toString(&conv_ok);
                    }

                    if (conv_ok) {
                        Error set_err = single_model->setFieldValue(pk_cpp_name_str, pk_any_val);
                        if (set_err) {
                            qWarning() << "backfillIdsFromLastInsertId (single): Error setting PK value for table " << QString::fromStdString(meta.table_name) << ", field " << QString::fromStdString(pk_cpp_name_str) << ". Error: " << QString::fromStdString(set_err.toString());
                        } else {
                            successfully_backfilled_models.push_back(single_model);
                        }
                    } else {
                        qWarning() << "backfillIdsFromLastInsertId (single): PK backfill conversion failed. SqlValue type:" << first_id_sql_val.typeName() << " to C++ type " << pk_cpp_type.name() << " for table " << QString::fromStdString(meta.table_name);
                    }
                }
            } else if ((db_driver_name_upper_std.find("MYSQL") != std::string::npos || db_driver_name_upper_std.find("MARIADB") != std::string::npos) && total_rows_affected_by_query > 0 && total_rows_affected_by_query <= static_cast<long long>(models_to_backfill_from.size()) &&
                       (!active_conflict_clause || (active_conflict_clause && (active_conflict_clause->action == OnConflictClause::Action::UpdateAllExcluded || active_conflict_clause->action == OnConflictClause::Action::UpdateSpecific)))) {
                bool ok_first_id_ll;
                long long first_id_ll = first_id_sql_val.toInt64(&ok_first_id_ll);
                if (!ok_first_id_ll) {
                    qWarning() << "backfillIdsFromLastInsertId (MySQL Batch): lastInsertId could not be converted to long long for table " << QString::fromStdString(meta.table_name);
                    return successfully_backfilled_models;
                }

                if (total_rows_affected_by_query == static_cast<long long>(models_to_backfill_from.size())) {
                    size_t persisted_model_idx = 0;
                    for (ModelBase *current_model : models_to_backfill_from) {
                        if (!current_model || !current_model->_is_persisted) continue;

                        long long current_model_id_ll = first_id_ll + static_cast<long long>(persisted_model_idx);
                        // Explicitly cast to the correct integer type for SqlValue constructor
                        cpporm_sqldriver::SqlValue current_id_sv(static_cast<int64_t>(current_model_id_ll));
                        std::any pk_any_val_seq;
                        bool conv_ok_seq = false;

                        if (pk_cpp_type == typeid(int))
                            pk_any_val_seq = current_id_sv.toInt32(&conv_ok_seq);
                        else if (pk_cpp_type == typeid(long long))
                            pk_any_val_seq = current_id_sv.toInt64(&conv_ok_seq);
                        else if (pk_cpp_type == typeid(unsigned int))
                            pk_any_val_seq = current_id_sv.toUInt32(&conv_ok_seq);
                        else if (pk_cpp_type == typeid(unsigned long long))
                            pk_any_val_seq = current_id_sv.toUInt64(&conv_ok_seq);
                        else {
                            pk_any_val_seq = current_id_sv.toString(&conv_ok_seq);
                        }

                        if (conv_ok_seq) {
                            Error set_err = current_model->setFieldValue(pk_cpp_name_str, pk_any_val_seq);
                            if (set_err) {
                                qWarning() << "backfillIdsFromLastInsertId (MySQL Batch): Error setting PK value for table " << QString::fromStdString(meta.table_name) << ". Error: " << QString::fromStdString(set_err.toString());
                            } else {
                                successfully_backfilled_models.push_back(current_model);
                            }
                        } else {
                            qWarning() << "backfillIdsFromLastInsertId (MySQL Batch): PK backfill conversion failed for sequential ID. SqlValue type:" << current_id_sv.typeName() << " to C++ type " << pk_cpp_type.name() << " for table " << QString::fromStdString(meta.table_name);
                        }
                        persisted_model_idx++;
                    }
                } else if (total_rows_affected_by_query > 0) {
                    ModelBase *first_persisted_model = nullptr;
                    for (ModelBase *m : models_to_backfill_from) {
                        if (m && m->_is_persisted) {
                            first_persisted_model = m;
                            break;
                        }
                    }

                    if (first_persisted_model) {
                        std::any pk_any_val;
                        bool conv_ok = false;
                        if (pk_cpp_type == typeid(int))
                            pk_any_val = first_id_sql_val.toInt32(&conv_ok);
                        else if (pk_cpp_type == typeid(long long))
                            pk_any_val = first_id_sql_val.toInt64(&conv_ok);
                        else {
                            pk_any_val = first_id_sql_val.toString(&conv_ok);
                        }
                        if (conv_ok) {
                            Error set_err = first_persisted_model->setFieldValue(pk_cpp_name_str, pk_any_val);
                            if (!set_err) successfully_backfilled_models.push_back(first_persisted_model);
                        }
                    }
                    qWarning() << "backfillIdsFromLastInsertId: lastInsertId may not be reliable for all rows (MySQL batch). Rows affected (" << total_rows_affected_by_query << ") != models persisted/attempted in batch (" << models_to_backfill_from.size()
                               << "). Table: " << QString::fromStdString(meta.table_name);
                }

            } else if (db_driver_name_upper_std.find("SQLITE") != std::string::npos && total_rows_affected_by_query == 1 && !models_to_backfill_from.empty()) {
                ModelBase *model_to_set = nullptr;
                for (ModelBase *m : models_to_backfill_from) {
                    if (m && m->_is_persisted) {
                        model_to_set = m;
                        break;
                    }
                }

                if (model_to_set) {
                    std::any pk_any_val;
                    bool conv_ok = false;
                    if (pk_cpp_type == typeid(int))
                        pk_any_val = first_id_sql_val.toInt32(&conv_ok);
                    else if (pk_cpp_type == typeid(long long))
                        pk_any_val = first_id_sql_val.toInt64(&conv_ok);
                    else {
                        pk_any_val = first_id_sql_val.toString(&conv_ok);
                    }

                    if (conv_ok) {
                        Error set_err = model_to_set->setFieldValue(pk_cpp_name_str, pk_any_val);
                        if (!set_err) successfully_backfilled_models.push_back(model_to_set);
                    }
                }
            } else {
                qWarning() << "backfillIdsFromLastInsertId: lastInsertId is not reliably applicable for this batch operation on driver " << QString::fromStdString(db_driver_name_upper_std) << " for table " << QString::fromStdString(meta.table_name)
                           << ". Models processed: " << models_to_backfill_from.size() << ", Rows affected by query: " << total_rows_affected_by_query;
            }
            return successfully_backfilled_models;
        }

    }  // namespace internal_batch_helpers
}  // namespace cpporm