// cpporm/session_static_utils.cpp
#include "cpporm/error.h"      // For Error, make_ok
#include "cpporm/model_base.h" // For FieldMeta in getSqlTypeForCppType
#include "cpporm/session.h"    // 主头文件

#include <QDebug>
#include <QMetaType>
#include <QSqlError> // For execute_query_internal
#include <QSqlQuery> // For execute_query_internal
#include <QVariant>
#include <any> // For anyToQueryValueForSessionConvenience, qvariantToAny

namespace cpporm {

// --- Static Helpers implementation ---
std::string Session::getSqlTypeForCppType(const FieldMeta &field_meta,
                                          const QString &driverName_upper) {
  if (!field_meta.db_type_hint.empty()) {
    return field_meta.db_type_hint;
  }
  const std::type_index &cpp_type = field_meta.cpp_type;

  if (cpp_type == typeid(int))
    return "INT";
  if (cpp_type == typeid(unsigned int)) {
    if (driverName_upper == "QPSQL" || driverName_upper == "QSQLITE")
      return "INTEGER";
    return "INT UNSIGNED";
  }
  if (cpp_type == typeid(long long))
    return "BIGINT";
  if (cpp_type == typeid(unsigned long long)) {
    if (driverName_upper == "QPSQL" || driverName_upper == "QSQLITE")
      return "BIGINT";
    return "BIGINT UNSIGNED";
  }
  if (cpp_type == typeid(float))
    return "FLOAT";
  if (cpp_type == typeid(double))
    return "DOUBLE PRECISION";
  if (cpp_type == typeid(bool)) {
    if (driverName_upper == "QPSQL" || driverName_upper == "QSQLITE")
      return "BOOLEAN";
    if (driverName_upper == "QMYSQL" || driverName_upper == "QMARIADB")
      return "TINYINT(1)";
    return "BOOLEAN";
  }
  if (cpp_type == typeid(std::string))
    return "TEXT";
  if (cpp_type == typeid(QDateTime)) {
    if (driverName_upper == "QPSQL")
      return "TIMESTAMP WITH TIME ZONE";
    if (driverName_upper == "QSQLITE")
      return "DATETIME";
    return "DATETIME";
  }
  if (cpp_type == typeid(QDate))
    return "DATE";
  if (cpp_type == typeid(QTime))
    return "TIME";
  if (cpp_type == typeid(QByteArray)) {
    if (driverName_upper == "QPSQL")
      return "BYTEA";
    if (driverName_upper == "QSQLITE")
      return "BLOB";
    return "BLOB";
  }

  qWarning() << "Session::getSqlTypeForCppType: Unknown C++ type "
             << QString::fromLocal8Bit(field_meta.cpp_type.name())
             << " for field '" << QString::fromStdString(field_meta.cpp_name)
             << "'. Defaulting to TEXT. Driver: " << driverName_upper;
  return "TEXT";
}

void Session::qvariantToAny(const QVariant &qv,
                            const std::type_index &target_cpp_type,
                            std::any &out_any, bool &out_conversion_ok) {
  out_conversion_ok = false;
  out_any.reset();

  if (qv.isNull() || !qv.isValid() || qv.userType() == QMetaType::UnknownType) {
    out_conversion_ok = true;
    return;
  }
  if (target_cpp_type == typeid(int)) {
    out_any = qv.toInt(&out_conversion_ok);
  } else if (target_cpp_type == typeid(long long)) {
    out_any = qv.toLongLong(&out_conversion_ok);
  } else if (target_cpp_type == typeid(unsigned int)) {
    out_any = qv.toUInt(&out_conversion_ok);
  } else if (target_cpp_type == typeid(unsigned long long)) {
    out_any = qv.toULongLong(&out_conversion_ok);
  } else if (target_cpp_type == typeid(double)) {
    out_any = qv.toDouble(&out_conversion_ok);
  } else if (target_cpp_type == typeid(float)) {
    out_any = qv.toFloat(&out_conversion_ok);
  } else if (target_cpp_type == typeid(bool)) {
    out_any = qv.toBool();
    out_conversion_ok = true;
  } else if (target_cpp_type == typeid(std::string)) {
    if (qv.canConvert<QString>()) {
      out_any = qv.toString().toStdString();
      out_conversion_ok = true;
    } else if (qv.typeId() == QMetaType::QByteArray) {
      QByteArray ba = qv.toByteArray();
      out_any = std::string(ba.constData(), static_cast<size_t>(ba.size()));
      out_conversion_ok = true;
    } else {
      qWarning() << "Session::qvariantToAny: Cannot convert QVariant type"
                 << qv.typeName() << "to std::string for target type"
                 << QString::fromLocal8Bit(target_cpp_type.name());
    }
  } else if (target_cpp_type == typeid(QDateTime)) {
    if (qv.canConvert<QDateTime>()) {
      out_any = qv.toDateTime();
      out_conversion_ok = qv.toDateTime().isValid();
    }
  } else if (target_cpp_type == typeid(QDate)) {
    if (qv.canConvert<QDate>()) {
      out_any = qv.toDate();
      out_conversion_ok = qv.toDate().isValid();
    }
  } else if (target_cpp_type == typeid(QTime)) {
    if (qv.canConvert<QTime>()) {
      out_any = qv.toTime();
      out_conversion_ok = qv.toTime().isValid();
    }
  } else if (target_cpp_type == typeid(QByteArray)) {
    if (qv.canConvert<QByteArray>()) {
      out_any = qv.toByteArray();
      out_conversion_ok = true;
    }
  } else {
    qWarning() << "Session::qvariantToAny: Unsupported C++ target type for "
                  "QVariant conversion: "
               << QString::fromLocal8Bit(target_cpp_type.name())
               << "from QVariant type" << qv.typeName();
  }
  if (!out_conversion_ok && qv.isValid() && !qv.isNull()) {
    qWarning()
        << "Session::qvariantToAny: Conversion failed for QVariant value ["
        << qv.toString() << "] of type" << qv.typeName() << "to C++ type"
        << QString::fromLocal8Bit(target_cpp_type.name());
  }
}

QueryValue Session::anyToQueryValueForSessionConvenience(const std::any &val) {
  if (!val.has_value())
    return nullptr;
  const auto &type = val.type();
  if (type == typeid(int))
    return std::any_cast<int>(val);
  if (type == typeid(long long))
    return std::any_cast<long long>(val);
  if (type == typeid(double))
    return std::any_cast<double>(val);
  if (type == typeid(std::string))
    return std::any_cast<std::string>(val);
  if (type == typeid(bool))
    return std::any_cast<bool>(val);
  if (type == typeid(QDateTime))
    return std::any_cast<QDateTime>(val);
  if (type == typeid(QDate))
    return std::any_cast<QDate>(val);
  if (type == typeid(QTime))
    return std::any_cast<QTime>(val);
  if (type == typeid(QByteArray))
    return std::any_cast<QByteArray>(val);
  if (type == typeid(const char *))
    return std::string(std::any_cast<const char *>(val));
  if (type == typeid(std::nullptr_t))
    return nullptr;
  if (type == typeid(float))
    return static_cast<double>(std::any_cast<float>(val));
  if (type == typeid(short))
    return static_cast<int>(std::any_cast<short>(val));
  if (type == typeid(char)) {
    return static_cast<int>(std::any_cast<char>(val));
  }
  if (type == typeid(unsigned char))
    return static_cast<int>(std::any_cast<unsigned char>(val));
  if (type == typeid(unsigned short))
    return static_cast<int>(std::any_cast<unsigned short>(val));
  if (type == typeid(unsigned int))
    return static_cast<long long>(std::any_cast<unsigned int>(val));
  if (type == typeid(unsigned long long))
    return static_cast<long long>(std::any_cast<unsigned long long>(val));
  qWarning() << "Session::anyToQueryValueForSessionConvenience: Unhandled "
                "std::any type:"
             << QString::fromLocal8Bit(val.type().name());
  return nullptr;
}

// --- Private Static execute_query_internal (implementation) ---
// 这现在是 Session 类的私有静态成员，它的唯一定义在此处。
std::pair<QSqlQuery, Error>
Session::execute_query_internal(QSqlDatabase db_conn_val_copy,
                                const QString &sql,
                                const QVariantList &bound_params) {
  if (!db_conn_val_copy.isOpen()) {
    if (!db_conn_val_copy.open()) {
      QSqlError err = db_conn_val_copy.lastError();
      return {QSqlQuery(db_conn_val_copy),
              Error(ErrorCode::ConnectionNotOpen,
                    "execute_query_internal: Failed to open database for query "
                    "execution on connection '" +
                        db_conn_val_copy.connectionName().toStdString() +
                        "': " + err.text().toStdString(),
                    err.nativeErrorCode().toInt())};
    }
  }
  QSqlQuery query(db_conn_val_copy);
  query.prepare(sql);
  QSqlError prepareError = query.lastError();
  if (prepareError.type() != QSqlError::NoError &&
      prepareError.type() != QSqlError::UnknownError) {
    if (!query.isValid() || (prepareError.type() > QSqlError::NoError &&
                             prepareError.type() < QSqlError::StatementError)) {
      return {query, Error(ErrorCode::StatementPreparationError,
                           "Failed to prepare SQL query: " +
                               prepareError.text().toStdString() +
                               " SQL: " + sql.toStdString(),
                           prepareError.nativeErrorCode().toInt())};
    }
  }
  for (const QVariant &param : bound_params) {
    query.addBindValue(param);
  }
  if (!query.exec()) {
    QSqlError err = query.lastError();
    QStringList params_str_list;
    for (const auto &v : bound_params)
      params_str_list << v.toString();
    return {query,
            Error(ErrorCode::QueryExecutionError,
                  "SQL query execution failed: " + err.text().toStdString() +
                      " (Driver: " + err.driverText().toStdString() +
                      ", DB: " + err.databaseText().toStdString() + ")" +
                      "\nSQL: " + sql.toStdString() +
                      "\nParams: " + params_str_list.join(", ").toStdString(),
                  err.nativeErrorCode().toInt(),
                  err.isValid() && !err.nativeErrorCode().isEmpty()
                      ? err.nativeErrorCode().toStdString()
                      : "")};
  }
  return {query, make_ok()};
}

} // namespace cpporm