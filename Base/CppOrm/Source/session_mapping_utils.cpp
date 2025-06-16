// cpporm/session_mapping_utils.cpp
#include "cpporm/model_base.h"
#include "cpporm/session.h"
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QMetaType>
#include <QSqlRecord>
#include <QTime>
#include <QVariant>

namespace cpporm {

Error Session::mapRowToModel(QSqlQuery &query, ModelBase &model,
                             const ModelMeta &meta) {
  QSqlRecord record = query.record();
  for (int i = 0; i < record.count(); ++i) {
    QString db_col_name_qstr = record.fieldName(i);
    const FieldMeta *field_meta =
        meta.findFieldByDbName(db_col_name_qstr.toStdString());

    if (!field_meta) {
      continue;
    }
    if (has_flag(field_meta->flags, FieldFlag::Association))
      continue;

    QVariant q_value = query.value(i);
    std::any cpp_value;

    if (q_value.isNull() || q_value.typeId() == QMetaType::UnknownType ||
        !q_value.isValid()) {
      // If DB value is NULL, set std::any to represent this (empty or specific
      // null type)
      Error set_err = model.setFieldValue(field_meta->cpp_name, std::any{});
      if (set_err) {
        qWarning() << "cpporm Session::mapRowToModel: Error setting "
                      "null/invalid field"
                   << QString::fromStdString(field_meta->cpp_name) << ":"
                   << set_err.toString().c_str();
      }
      continue;
    }

    bool conversion_ok = false;
    if (field_meta->cpp_type == typeid(QDateTime)) {
      if (q_value.canConvert<QDateTime>()) {
        cpp_value = q_value.toDateTime();
        conversion_ok = true;
      }
    } else if (field_meta->cpp_type == typeid(QDate)) {
      if (q_value.canConvert<QDate>()) {
        cpp_value = q_value.toDate();
        conversion_ok = true;
      }
    } else if (field_meta->cpp_type == typeid(QTime)) {
      if (q_value.canConvert<QTime>()) {
        cpp_value = q_value.toTime();
        conversion_ok = true;
      }
    } else if (field_meta->cpp_type == typeid(std::string)) {
      if (q_value.typeId() == QMetaType::QByteArray) {
        // If the database returns bytes (e.g. for TEXT/BLOB treated as string)
        // assume UTF-8. If it's truly binary, std::string might not be the best
        // C++ type.
        QByteArray ba = q_value.toByteArray();
        cpp_value = std::string(ba.constData(), static_cast<size_t>(ba.size()));
        conversion_ok = true;
      } else if (q_value.canConvert<QString>()) {
        QByteArray ba = q_value.toString().toUtf8();
        cpp_value = std::string(ba.constData(), static_cast<size_t>(ba.size()));
        conversion_ok = true;
      } else {
        qWarning()
            << "cpporm Session::mapRowToModel: QVariant for std::string field "
            << QString::fromStdString(field_meta->cpp_name)
            << " cannot be converted to QString or QByteArray. DB value type: "
            << q_value.typeName();
      }
    } else if (field_meta->cpp_type == typeid(int)) {
      cpp_value = q_value.toInt(&conversion_ok);
    } else if (field_meta->cpp_type == typeid(long long)) {
      cpp_value = q_value.toLongLong(&conversion_ok);
    } else if (field_meta->cpp_type == typeid(unsigned int)) {
      cpp_value = q_value.toUInt(&conversion_ok);
    } else if (field_meta->cpp_type == typeid(unsigned long long)) {
      cpp_value = q_value.toULongLong(&conversion_ok);
    } else if (field_meta->cpp_type == typeid(double)) {
      cpp_value = q_value.toDouble(&conversion_ok);
    } else if (field_meta->cpp_type == typeid(float)) {
      cpp_value = q_value.toFloat(&conversion_ok);
    } else if (field_meta->cpp_type == typeid(bool)) {
      cpp_value = q_value.toBool();
      conversion_ok = true;
    } else if (field_meta->cpp_type == typeid(QByteArray)) {
      cpp_value = q_value.toByteArray();
      conversion_ok = true;
    } else {
      qWarning()
          << "cpporm Session::mapRowToModel: Unsupported C++ type for field"
          << QString::fromStdString(field_meta->cpp_name)
          << "Type:" << field_meta->cpp_type.name();
      continue;
    }

    if (!conversion_ok) {
      qWarning() << "cpporm Session::mapRowToModel: QVariant to C++ type "
                    "conversion failed for field"
                 << QString::fromStdString(field_meta->cpp_name)
                 << ". DB value:" << q_value.toString()
                 << "(QVariant type:" << q_value.typeName()
                 << ", Target C++ type:" << field_meta->cpp_type.name() << ")";
      Error set_err = model.setFieldValue(field_meta->cpp_name, std::any{});
      if (set_err) {
        // Potentially return error or log and continue
      }
      continue;
    }

    Error set_err = model.setFieldValue(field_meta->cpp_name, cpp_value);
    if (set_err) {
      qWarning() << "cpporm Session::mapRowToModel: Error setting field"
                 << QString::fromStdString(field_meta->cpp_name)
                 << "after conversion:" << set_err.toString().c_str();
    }
  }
  model._is_persisted = true;
  return make_ok();
}

cpporm::internal::SessionModelDataForWrite
Session::extractModelData(const ModelBase &model_instance,
                          const ModelMeta &meta, bool for_update,
                          bool include_timestamps_even_if_null) {
  cpporm::internal::SessionModelDataForWrite data;

  for (const auto &field_meta : meta.fields) {
    if (has_flag(field_meta.flags, FieldFlag::Association))
      continue;

    bool is_pk = has_flag(field_meta.flags, FieldFlag::PrimaryKey);
    bool is_auto_inc = has_flag(field_meta.flags, FieldFlag::AutoIncrement);

    std::any val_any = model_instance.getFieldValue(field_meta.cpp_name);

    QVariant q_val;
    if (!val_any.has_value()) {
      q_val = QVariant(QMetaType(
          QMetaType::UnknownType)); // Explicitly SQL NULL representation
    } else if (val_any.type() == typeid(int)) {
      q_val.setValue(std::any_cast<int>(val_any));
    } else if (val_any.type() == typeid(long long)) {
      q_val.setValue(std::any_cast<long long>(val_any));
    } else if (val_any.type() == typeid(unsigned int)) {
      q_val.setValue(std::any_cast<unsigned int>(val_any));
    } else if (val_any.type() == typeid(unsigned long long)) {
      q_val.setValue(std::any_cast<unsigned long long>(val_any));
    } else if (val_any.type() == typeid(double)) {
      q_val.setValue(std::any_cast<double>(val_any));
    } else if (val_any.type() == typeid(float)) {
      q_val.setValue(std::any_cast<float>(val_any));
    } else if (val_any.type() == typeid(std::string)) {
      q_val.setValue(
          QString::fromUtf8(std::any_cast<std::string>(val_any).c_str()));
    } else if (val_any.type() == typeid(bool)) {
      q_val.setValue(std::any_cast<bool>(val_any));
    } else if (val_any.type() == typeid(QByteArray)) {
      q_val.setValue(std::any_cast<QByteArray>(val_any));
    } else if (val_any.type() == typeid(QDate)) {
      q_val.setValue(std::any_cast<QDate>(val_any));
    } else if (val_any.type() == typeid(QTime)) {
      q_val.setValue(std::any_cast<QTime>(val_any));
    } else if (val_any.type() == typeid(QDateTime)) {
      q_val.setValue(std::any_cast<QDateTime>(val_any));
    } else {
      qWarning() << "cpporm Session::extractModelData: Unsupported C++ type "
                    "in model field "
                 << QString::fromStdString(field_meta.cpp_name);
      continue;
    }

    if (is_pk) {
      if (q_val.isValid() && !q_val.isNull()) {
        data.primary_key_fields[QString::fromStdString(field_meta.db_name)] =
            q_val;
      }
      if (is_auto_inc) {
        data.has_auto_increment_pk = true;
        data.auto_increment_pk_name_db =
            QString::fromStdString(field_meta.db_name);
        data.pk_cpp_name_for_autoincrement = field_meta.cpp_name;
        data.pk_cpp_type_for_autoincrement = field_meta.cpp_type;
      }
    }

    if (for_update) {
      if (is_pk || has_flag(field_meta.flags, FieldFlag::CreatedAt)) {
        continue;
      }
      if (has_flag(field_meta.flags, FieldFlag::UpdatedAt) &&
          !include_timestamps_even_if_null && !val_any.has_value()) {
        continue;
      }
    } else { // Create operation
      if (is_auto_inc && is_pk) {
        continue;
      }
      if ((has_flag(field_meta.flags, FieldFlag::CreatedAt) ||
           has_flag(field_meta.flags, FieldFlag::UpdatedAt)) &&
          !include_timestamps_even_if_null && !val_any.has_value()) {
        continue;
      }
    }
    data.fields_to_write[QString::fromStdString(field_meta.db_name)] = q_val;
  }
  return data;
}

void Session::autoSetTimestamps(ModelBase &model_instance,
                                const ModelMeta &meta, bool is_create_op) {
  QDateTime current_ts = QDateTime::currentDateTimeUtc();

  if (is_create_op) {
    if (const FieldMeta *created_at_field =
            meta.findFieldWithFlag(FieldFlag::CreatedAt)) {
      if (created_at_field->cpp_type == typeid(QDateTime)) {
        std::any current_val =
            model_instance.getFieldValue(created_at_field->cpp_name);
        if (!current_val.has_value() ||
            (current_val.type() == typeid(QDateTime) &&
             !std::any_cast<QDateTime>(current_val).isValid())) {
          model_instance.setFieldValue(created_at_field->cpp_name, current_ts);
        }
      }
    }
  }

  if (const FieldMeta *updated_at_field =
          meta.findFieldWithFlag(FieldFlag::UpdatedAt)) {
    if (updated_at_field->cpp_type == typeid(QDateTime)) {
      model_instance.setFieldValue(updated_at_field->cpp_name, current_ts);
    }
  }
}

} // namespace cpporm