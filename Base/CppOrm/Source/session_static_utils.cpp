// Base/CppOrm/Source/session_static_utils.cpp
#include <QDebug>
#include <QMetaType>
#include <QVariant>
#include <any>
#include <variant>

#include "cpporm/error.h"
#include "cpporm/model_base.h"
#include "cpporm/session.h"
#include "sqldriver/sql_database.h"
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_query.h"
#include "sqldriver/sql_value.h"

namespace cpporm {

    std::string Session::getSqlTypeForCppType(const FieldMeta &field_meta, const QString &driverName_upper_qstr) {
        std::string driverName_upper = driverName_upper_qstr.toStdString();
        if (!field_meta.db_type_hint.empty()) {
            return field_meta.db_type_hint;
        }
        const std::type_index &cpp_type = field_meta.cpp_type;

        if (cpp_type == typeid(int)) return "INT";
        if (cpp_type == typeid(unsigned int)) {
            if (driverName_upper == "QPSQL" || driverName_upper == "QSQLITE") return "INTEGER";
            return "INT UNSIGNED";
        }
        if (cpp_type == typeid(int64_t)) return "BIGINT";  // FIX: was long long
        if (cpp_type == typeid(unsigned long long)) {
            if (driverName_upper == "QPSQL" || driverName_upper == "QSQLITE") return "BIGINT";
            return "BIGINT UNSIGNED";
        }
        if (cpp_type == typeid(float)) return "FLOAT";
        if (cpp_type == typeid(double)) return "DOUBLE PRECISION";
        if (cpp_type == typeid(bool)) {
            if (driverName_upper == "QPSQL" || driverName_upper == "QSQLITE") return "BOOLEAN";
            if (driverName_upper == "QMYSQL" || driverName_upper == "QMARIADB") return "TINYINT(1)";
            return "BOOLEAN";
        }
        if (cpp_type == typeid(std::string)) return "TEXT";
        if (cpp_type == typeid(QDateTime)) {
            if (driverName_upper == "QPSQL") return "TIMESTAMP WITH TIME ZONE";
            if (driverName_upper == "QSQLITE") return "DATETIME";
            return "DATETIME";
        }
        if (cpp_type == typeid(QDate)) return "DATE";
        if (cpp_type == typeid(QTime)) return "TIME";
        if (cpp_type == typeid(QByteArray)) {
            if (driverName_upper == "QPSQL") return "BYTEA";
            if (driverName_upper == "QSQLITE") return "BLOB";
            return "BLOB";
        }

        qWarning() << "Session::getSqlTypeForCppType: Unknown C++ type " << QString::fromLocal8Bit(field_meta.cpp_type.name()) << " for field '" << QString::fromStdString(field_meta.cpp_name) << "'. Defaulting to TEXT. Driver: " << driverName_upper_qstr;
        return "TEXT";
    }

    void Session::qvariantToAny(const QVariant &qv, const std::type_index &target_cpp_type, std::any &out_any, bool &out_conversion_ok) {
        out_conversion_ok = false;
        out_any.reset();

        if (qv.isNull() || !qv.isValid() ||
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            qv.typeId() == QMetaType::UnknownType
#else
            qv.type() == QVariant::Invalid || qv.userType() == QMetaType::UnknownType
#endif
        ) {
            out_conversion_ok = true;
            return;
        }
        if (target_cpp_type == typeid(int)) {
            out_any = qv.toInt(&out_conversion_ok);
        } else if (target_cpp_type == typeid(int64_t)) {  // FIX: was long long
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
            if (
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                QMetaType(qv.typeId()).flags().testFlag(QMetaType::IsPointer) ||  // Check if it's a pointer type
                QMetaType(qv.typeId()).flags().testFlag(QMetaType::IsGadget)      // Or a gadget
#else
                qv.type() == QVariant::UserType  // Check if it's a user type (often pointers or gadgets in Qt 5)
#endif
                // Add more specific checks if needed for custom types stored in QVariant
            ) {
                qWarning() << "Session::qvariantToAny: Attempting to convert a complex QVariant type" << qv.typeName() << "to std::string. This might not be meaningful.";
                // Fallback or specific handling if you know how to stringify it
                out_any = qv.toString().toStdString();  // Fallback to QVariant::toString()
                out_conversion_ok = true;               // Assume QVariant::toString() is always "successful"
            } else if (qv.canConvert<QString>()) {
                out_any = qv.toString().toStdString();
                out_conversion_ok = true;
            } else if (
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                qv.typeId() == QMetaType::QByteArray
#else
                qv.type() == QVariant::ByteArray
#endif
            ) {
                QByteArray ba = qv.toByteArray();
                out_any = std::string(ba.constData(), static_cast<size_t>(ba.size()));
                out_conversion_ok = true;
            } else {
                qWarning() << "Session::qvariantToAny: Cannot convert QVariant type" << qv.typeName() << "to std::string for target type" << QString::fromLocal8Bit(target_cpp_type.name());
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
                       << QString::fromLocal8Bit(target_cpp_type.name()) << "from QVariant type" << qv.typeName();
        }
        if (!out_conversion_ok && qv.isValid() && !qv.isNull()) {
            qWarning() << "Session::qvariantToAny: Conversion failed for QVariant value [" << qv.toString() << "] of type" << qv.typeName() << "to C++ type" << QString::fromLocal8Bit(target_cpp_type.name());
        }
    }

    QueryValue Session::anyToQueryValueForSessionConvenience(const std::any &val) {
        if (!val.has_value()) return nullptr;
        const auto &type = val.type();
        if (type == typeid(int)) return std::any_cast<int>(val);
        if (type == typeid(int64_t)) return static_cast<long long>(std::any_cast<int64_t>(val));  // FIX: was long long, ensure cast to variant type
        if (type == typeid(double)) return std::any_cast<double>(val);
        if (type == typeid(std::string)) return std::any_cast<std::string>(val);
        if (type == typeid(bool)) return std::any_cast<bool>(val);
        if (type == typeid(QDateTime)) return std::any_cast<QDateTime>(val);
        if (type == typeid(QDate)) return std::any_cast<QDate>(val);
        if (type == typeid(QTime)) return std::any_cast<QTime>(val);
        if (type == typeid(QByteArray)) return std::any_cast<QByteArray>(val);
        if (type == typeid(const char *)) return std::string(std::any_cast<const char *>(val));
        if (type == typeid(std::nullptr_t)) return nullptr;
        if (type == typeid(float)) return static_cast<double>(std::any_cast<float>(val));
        if (type == typeid(short)) return static_cast<int>(std::any_cast<short>(val));
        if (type == typeid(char)) {
            return static_cast<int>(std::any_cast<char>(val));
        }
        if (type == typeid(unsigned char)) return static_cast<int>(std::any_cast<unsigned char>(val));
        if (type == typeid(unsigned short)) return static_cast<int>(std::any_cast<unsigned short>(val));
        if (type == typeid(unsigned int)) return static_cast<long long>(std::any_cast<unsigned int>(val));
        if (type == typeid(unsigned long long)) return static_cast<long long>(std::any_cast<unsigned long long>(val));

        qWarning() << "Session::anyToQueryValueForSessionConvenience: Unhandled "
                      "std::any type:"
                   << QString::fromLocal8Bit(val.type().name());
        return nullptr;
    }

    cpporm_sqldriver::SqlValue Session::queryValueToSqlValue(const QueryValue &qv) {
        if (std::holds_alternative<std::nullptr_t>(qv)) return cpporm_sqldriver::SqlValue();
        if (std::holds_alternative<int>(qv)) return cpporm_sqldriver::SqlValue(static_cast<int32_t>(std::get<int>(qv)));
        if (std::holds_alternative<long long>(qv)) return cpporm_sqldriver::SqlValue(static_cast<int64_t>(std::get<long long>(qv)));
        if (std::holds_alternative<double>(qv)) return cpporm_sqldriver::SqlValue(std::get<double>(qv));
        if (std::holds_alternative<std::string>(qv)) return cpporm_sqldriver::SqlValue(std::get<std::string>(qv));
        if (std::holds_alternative<bool>(qv)) return cpporm_sqldriver::SqlValue(std::get<bool>(qv));
        if (std::holds_alternative<QDateTime>(qv)) return cpporm_sqldriver::SqlValue(std::get<QDateTime>(qv));
        if (std::holds_alternative<QDate>(qv)) return cpporm_sqldriver::SqlValue(std::get<QDate>(qv));
        if (std::holds_alternative<QTime>(qv)) return cpporm_sqldriver::SqlValue(std::get<QTime>(qv));
        if (std::holds_alternative<QByteArray>(qv)) return cpporm_sqldriver::SqlValue(std::get<QByteArray>(qv));
        if (std::holds_alternative<SubqueryExpression>(qv)) {
            qWarning("Session::queryValueToSqlValue: SubqueryExpression cannot be directly converted to a single SqlValue for binding. This usually indicates a logic error where a subquery is being treated as a simple bind value.");
            return cpporm_sqldriver::SqlValue();
        }
        qWarning("Session::queryValueToSqlValue: Unhandled QueryValue variant type during conversion to SqlValue.");
        return cpporm_sqldriver::SqlValue();
    }

    QueryValue Session::sqlValueToQueryValue(const cpporm_sqldriver::SqlValue &sv) {
        if (sv.isNull()) return nullptr;
        bool ok = false;
        switch (sv.type()) {
            case cpporm_sqldriver::SqlValueType::Bool:
                return sv.toBool(&ok);
            case cpporm_sqldriver::SqlValueType::Int8:
            case cpporm_sqldriver::SqlValueType::UInt8:
            case cpporm_sqldriver::SqlValueType::Int16:
            case cpporm_sqldriver::SqlValueType::UInt16:
            case cpporm_sqldriver::SqlValueType::Int32:
                return sv.toInt32(&ok);
            case cpporm_sqldriver::SqlValueType::UInt32:
                return static_cast<long long>(sv.toUInt32(&ok));
            case cpporm_sqldriver::SqlValueType::Int64:
                return sv.toInt64(&ok);
            case cpporm_sqldriver::SqlValueType::UInt64:
                return static_cast<long long>(sv.toUInt64(&ok));
            case cpporm_sqldriver::SqlValueType::Float:
            case cpporm_sqldriver::SqlValueType::Double:
            case cpporm_sqldriver::SqlValueType::LongDouble:
                return sv.toDouble(&ok);
            case cpporm_sqldriver::SqlValueType::String:
            case cpporm_sqldriver::SqlValueType::FixedString:
            case cpporm_sqldriver::SqlValueType::CharacterLargeObject:
                return sv.toString(&ok);
            case cpporm_sqldriver::SqlValueType::ByteArray:
            case cpporm_sqldriver::SqlValueType::BinaryLargeObject:
                return sv.toByteArray(&ok);
            case cpporm_sqldriver::SqlValueType::Date:
                return sv.toDate(&ok);
            case cpporm_sqldriver::SqlValueType::Time:
                return sv.toTime(&ok);
            case cpporm_sqldriver::SqlValueType::DateTime:
            case cpporm_sqldriver::SqlValueType::Timestamp:
                return sv.toDateTime(&ok);
            case cpporm_sqldriver::SqlValueType::Decimal:
            case cpporm_sqldriver::SqlValueType::Numeric:
                {
                    double d_val = sv.toDouble(&ok);
                    if (ok) return d_val;
                    std::string s_val = sv.toString(&ok);
                    if (ok) return s_val;
                    qWarning() << "Session::sqlValueToQueryValue: Could not convert Decimal/Numeric SqlValue to double or string.";
                }
                break;
            case cpporm_sqldriver::SqlValueType::Json:
                return sv.toString(&ok);
            default:
                qWarning() << "Session::sqlValueToQueryValue: Unhandled SqlValueType: " << static_cast<int>(sv.type()) << " (" << sv.typeName() << "). Attempting toString().";
                std::string s_val = sv.toString(&ok);
                if (ok) return s_val;
        }
        if (!ok && !sv.isNull()) {
            qWarning() << "Session::sqlValueToQueryValue: Conversion from SqlValue (type: " << sv.typeName() << ", value: " << QString::fromStdString(sv.toString()) << ") to a QueryValue alternative failed.";
        }
        return nullptr;
    }

    std::pair<cpporm_sqldriver::SqlQuery, Error> Session::execute_query_internal(cpporm_sqldriver::SqlDatabase &db_handle, const std::string &sql_std_str, const std::vector<cpporm_sqldriver::SqlValue> &bound_params) {
        if (!db_handle.isOpen()) {
            qWarning() << "Session::execute_query_internal: Database handle for connection '" << QString::fromStdString(db_handle.connectionName()) << "' is not open. Attempting to open...";
            if (!db_handle.open()) {
                cpporm_sqldriver::SqlError err = db_handle.lastError();
                return std::make_pair(cpporm_sqldriver::SqlQuery(db_handle),
                                      Error(ErrorCode::ConnectionNotOpen,
                                            "execute_query_internal: Failed to open database for query "
                                            "execution on connection '" +
                                                db_handle.connectionName() + "': " + err.text(),
                                            err.nativeErrorCodeNumeric()));
            }
        }

        cpporm_sqldriver::SqlQuery query(db_handle);

        if (!query.prepare(sql_std_str)) {
            cpporm_sqldriver::SqlError prepareError = query.lastError();
            return std::make_pair(std::move(query), Error(ErrorCode::StatementPreparationError, "Failed to prepare SQL query: " + prepareError.text() + " SQL: " + sql_std_str, prepareError.nativeErrorCodeNumeric()));
        }

        for (size_t i = 0; i < bound_params.size(); ++i) {
            query.bindValue(static_cast<int>(i), bound_params[i]);
        }

        if (!query.exec()) {
            cpporm_sqldriver::SqlError execError = query.lastError();
            std::string params_debug_str;
            for (const auto &p_sv : bound_params) {
                bool conv_ok = false;
                params_debug_str += p_sv.toString(&conv_ok) + ", ";
            }
            if (!params_debug_str.empty()) params_debug_str.resize(params_debug_str.length() - 2);

            return std::make_pair(std::move(query),
                                  Error(ErrorCode::QueryExecutionError, "SQL query execution failed: " + execError.text() + " (Native Code: " + execError.nativeErrorCode() + ")" + "\nSQL: " + sql_std_str + "\nParams: [" + params_debug_str + "]", execError.nativeErrorCodeNumeric(), ""));
        }
        return std::make_pair(std::move(query), make_ok());
    }

}  // namespace cpporm