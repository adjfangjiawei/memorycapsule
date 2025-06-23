#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QMetaType>

#include "cpporm/model_base.h"
#include "cpporm/session.h"
// #include <QSqlRecord> // No longer using QSqlRecord
#include <QTime>
#include <QVariant>  // Still used for std::any <-> QueryValue <-> SqlValue intermediate if needed

#include "sqldriver/sql_field.h"   // SqlField (from SqlRecord)
#include "sqldriver/sql_query.h"   // SqlQuery
#include "sqldriver/sql_record.h"  // SqlRecord (from SqlQuery)
#include "sqldriver/sql_value.h"   // SqlValue

namespace cpporm {

    Error Session::mapRowToModel(cpporm_sqldriver::SqlQuery &query, ModelBase &model, const ModelMeta &meta) {
        cpporm_sqldriver::SqlRecord record_meta = query.recordMetadata();  // Get metadata once
        if (record_meta.isEmpty()) {
            qWarning() << "cpporm Session::mapRowToModel: Query returned no record metadata for table " << QString::fromStdString(meta.table_name);
            return Error(ErrorCode::MappingError, "Query returned no record metadata.");
        }

        for (int i = 0; i < record_meta.count(); ++i) {
            cpporm_sqldriver::SqlField col_meta_field = record_meta.field(i);  // SqlRecord::field()
            std::string db_col_name_std = col_meta_field.name();

            const FieldMeta *model_field_meta = meta.findFieldByDbName(db_col_name_std);

            if (!model_field_meta) {
                continue;  // Column from DB not mapped in model
            }
            if (has_flag(model_field_meta->flags, FieldFlag::Association)) {
                continue;  // Skip association placeholder fields
            }

            cpporm_sqldriver::SqlValue sql_val = query.value(i);  // SqlQuery::value()
            std::any cpp_value;
            bool conversion_ok = false;

            if (sql_val.isNull()) {
                conversion_ok = true;  // std::any will be empty, representing NULL
            } else {
                const std::type_index &target_cpp_type = model_field_meta->cpp_type;

                // *** FIX START ***
                // 使用 SqlValue 的 toType() 方法，而不是直接 std::any_cast，以允许跨类型转换。
                if (target_cpp_type == typeid(int)) {
                    cpp_value = sql_val.toInt32(&conversion_ok);
                } else if (target_cpp_type == typeid(long long)) {
                    cpp_value = sql_val.toInt64(&conversion_ok);
                } else if (target_cpp_type == typeid(unsigned int)) {
                    cpp_value = sql_val.toUInt32(&conversion_ok);
                } else if (target_cpp_type == typeid(unsigned long long)) {
                    cpp_value = sql_val.toUInt64(&conversion_ok);
                } else if (target_cpp_type == typeid(double)) {
                    cpp_value = sql_val.toDouble(&conversion_ok);
                } else if (target_cpp_type == typeid(float)) {
                    cpp_value = sql_val.toFloat(&conversion_ok);
                } else if (target_cpp_type == typeid(bool)) {
                    cpp_value = sql_val.toBool(&conversion_ok);
                } else if (target_cpp_type == typeid(std::string)) {
                    cpp_value = sql_val.toString(&conversion_ok);
                } else if (target_cpp_type == typeid(QDateTime)) {
                    cpp_value = sql_val.toDateTime(&conversion_ok);
                } else if (target_cpp_type == typeid(QDate)) {
                    cpp_value = sql_val.toDate(&conversion_ok);
                } else if (target_cpp_type == typeid(QTime)) {
                    cpp_value = sql_val.toTime(&conversion_ok);
                } else if (target_cpp_type == typeid(QByteArray)) {
                    cpp_value = sql_val.toByteArray(&conversion_ok);
                } else {
                    qWarning() << "cpporm Session::mapRowToModel: Unsupported C++ type for field" << QString::fromStdString(model_field_meta->cpp_name) << "Type:" << model_field_meta->cpp_type.name();
                    continue;
                }
                // *** FIX END ***
            }

            if (!conversion_ok) {
                std::string sv_str = sql_val.isNull() ? "NULL" : sql_val.toString();
                qWarning() << "cpporm Session::mapRowToModel: SqlValue to C++ type conversion failed for field" << QString::fromStdString(model_field_meta->cpp_name) << ". DB value (as string):" << QString::fromStdString(sv_str) << "(SqlValue type:" << sql_val.typeName()
                           << ", Target C++ type:" << model_field_meta->cpp_type.name() << ")";
                // Set to empty std::any to indicate failed conversion for a non-null value
                Error set_err = model.setFieldValue(model_field_meta->cpp_name, std::any{});
                if (set_err) { /* Log or handle */
                }
                continue;
            }

            Error set_err = model.setFieldValue(model_field_meta->cpp_name, cpp_value);
            if (set_err) {
                qWarning() << "cpporm Session::mapRowToModel: Error setting field" << QString::fromStdString(model_field_meta->cpp_name) << "after conversion:" << set_err.toString().c_str();
            }
        }
        model._is_persisted = true;  // Mark model as persisted after successful mapping
        return make_ok();
    }

    cpporm::internal::SessionModelDataForWrite Session::extractModelData(const ModelBase &model_instance, const ModelMeta &meta, bool for_update, bool include_timestamps_even_if_null) {
        cpporm::internal::SessionModelDataForWrite data;  // Holds SqlValue now

        for (const auto &field_meta : meta.fields) {
            if (has_flag(field_meta.flags, FieldFlag::Association)) {
                continue;
            }

            bool is_pk = has_flag(field_meta.flags, FieldFlag::PrimaryKey);
            bool is_auto_inc = has_flag(field_meta.flags, FieldFlag::AutoIncrement);

            std::any val_any = model_instance.getFieldValue(field_meta.cpp_name);
            cpporm_sqldriver::SqlValue sql_val_to_write;

            if (!val_any.has_value()) {
                sql_val_to_write = cpporm_sqldriver::SqlValue();  // Null SqlValue
            } else {
                // Convert std::any to SqlValue using its type
                const auto &type = val_any.type();
                if (type == typeid(int))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(static_cast<int32_t>(std::any_cast<int>(val_any)));
                else if (type == typeid(long long))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(static_cast<int64_t>(std::any_cast<long long>(val_any)));
                else if (type == typeid(unsigned int))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(static_cast<uint32_t>(std::any_cast<unsigned int>(val_any)));
                else if (type == typeid(unsigned long long))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(static_cast<uint64_t>(std::any_cast<unsigned long long>(val_any)));
                else if (type == typeid(double))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(std::any_cast<double>(val_any));
                else if (type == typeid(float))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(std::any_cast<float>(val_any));
                else if (type == typeid(bool))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(std::any_cast<bool>(val_any));
                else if (type == typeid(std::string))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(std::any_cast<std::string>(val_any));
                else if (type == typeid(QDateTime))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(std::any_cast<QDateTime>(val_any));
                else if (type == typeid(QDate))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(std::any_cast<QDate>(val_any));
                else if (type == typeid(QTime))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(std::any_cast<QTime>(val_any));
                else if (type == typeid(QByteArray))
                    sql_val_to_write = cpporm_sqldriver::SqlValue(std::any_cast<QByteArray>(val_any));
                else {
                    qWarning() << "cpporm Session::extractModelData: Unsupported C++ type " << QString::fromLocal8Bit(type.name()) << " in model field " << QString::fromStdString(field_meta.cpp_name) << "for SqlValue conversion.";
                    continue;  // Skip this field
                }
            }

            if (is_pk) {
                if (sql_val_to_write.isValid() && !sql_val_to_write.isNull()) {
                    // primary_key_fields map uses std::string as key
                    data.primary_key_fields[field_meta.db_name] = sql_val_to_write;
                }
                if (is_auto_inc) {
                    data.has_auto_increment_pk = true;
                    // auto_increment_pk_name_db is std::string
                    data.auto_increment_pk_name_db = field_meta.db_name;
                    data.pk_cpp_name_for_autoincrement = field_meta.cpp_name;
                    data.pk_cpp_type_for_autoincrement = field_meta.cpp_type;
                }
            }

            if (for_update) {  // Update operation
                if (is_pk || has_flag(field_meta.flags, FieldFlag::CreatedAt)) {
                    continue;  // Skip PKs and CreatedAt for updates
                }
                // For UpdatedAt, include it if it's non-null OR if include_timestamps_even_if_null is true
                if (has_flag(field_meta.flags, FieldFlag::UpdatedAt) && !include_timestamps_even_if_null && sql_val_to_write.isNull()) {
                    continue;
                }
            } else {  // Create operation
                if (is_auto_inc && is_pk) {
                    continue;  // Skip auto-increment PK for creates (DB handles it)
                }
                // For CreatedAt/UpdatedAt on create, include if non-null OR if include_timestamps_even_if_null
                if ((has_flag(field_meta.flags, FieldFlag::CreatedAt) || has_flag(field_meta.flags, FieldFlag::UpdatedAt)) && !include_timestamps_even_if_null && sql_val_to_write.isNull()) {
                    continue;
                }
            }
            // fields_to_write map uses std::string as key
            data.fields_to_write[field_meta.db_name] = sql_val_to_write;
        }
        return data;
    }

    void Session::autoSetTimestamps(ModelBase &model_instance, const ModelMeta &meta, bool is_create_op) {
        // QDateTime is still used as the C++ type for timestamps
        QDateTime current_ts = QDateTime::currentDateTimeUtc();

        if (is_create_op) {
            if (const FieldMeta *created_at_field = meta.findFieldWithFlag(FieldFlag::CreatedAt)) {
                if (created_at_field->cpp_type == typeid(QDateTime)) {
                    std::any current_val = model_instance.getFieldValue(created_at_field->cpp_name);
                    // Set if current value is not set (empty any) or if it's an invalid QDateTime
                    if (!current_val.has_value() || (current_val.type() == typeid(QDateTime) && !std::any_cast<QDateTime>(current_val).isValid())) {
                        model_instance.setFieldValue(created_at_field->cpp_name, current_ts);
                    }
                }
            }
        }

        if (const FieldMeta *updated_at_field = meta.findFieldWithFlag(FieldFlag::UpdatedAt)) {
            if (updated_at_field->cpp_type == typeid(QDateTime)) {
                model_instance.setFieldValue(updated_at_field->cpp_name, current_ts);
            }
        }
    }

}  // namespace cpporm