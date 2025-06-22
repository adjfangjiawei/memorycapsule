#include <mysql/mysql.h>  // For MYSQL_TIME and ::MYSQL_TYPE_ constants

#include <chrono>  // For std::chrono conversions

#include "sqldriver/mysql/mysql_driver_helper.h"

namespace cpporm_sqldriver {
    namespace mysql_helper {

        mysql_protocol::MySqlNativeValue sqlValueToMySqlNativeValue(const SqlValue& value) {
            mysql_protocol::MySqlNativeValue native_value;  // 默认构造为 monostate (NULL)
            native_value.original_mysql_type = ::MYSQL_TYPE_NULL;
            bool conversion_successful = true;  // 跟踪 SqlValue -> C++类型的转换是否成功

            if (value.isNull()) {
                return native_value;  // 已是 NULL
            }

            switch (value.type()) {
                case SqlValueType::Bool:
                    native_value.data = value.toBool(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_TINY;
                    break;
                case SqlValueType::Int8:
                    native_value.data = value.toInt8(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_TINY;
                    break;
                case SqlValueType::UInt8:
                    native_value.data = value.toUInt8(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_TINY;
                    native_value.original_mysql_flags = UNSIGNED_FLAG;
                    break;
                case SqlValueType::Int16:
                    native_value.data = value.toInt16(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_SHORT;
                    break;
                case SqlValueType::UInt16:
                    native_value.data = value.toUInt16(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_SHORT;
                    native_value.original_mysql_flags = UNSIGNED_FLAG;
                    break;
                case SqlValueType::Int32:
                    native_value.data = value.toInt32(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_LONG;
                    break;
                case SqlValueType::UInt32:
                    native_value.data = value.toUInt32(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_LONG;
                    native_value.original_mysql_flags = UNSIGNED_FLAG;
                    break;
                case SqlValueType::Int64:
                    native_value.data = value.toInt64(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_LONGLONG;
                    break;
                case SqlValueType::UInt64:
                    native_value.data = value.toUInt64(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_LONGLONG;
                    native_value.original_mysql_flags = UNSIGNED_FLAG;
                    break;
                case SqlValueType::Float:
                    native_value.data = value.toFloat(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_FLOAT;
                    break;
                case SqlValueType::Double:
                    native_value.data = value.toDouble(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_DOUBLE;
                    break;
                case SqlValueType::String:
                case SqlValueType::FixedString:           // MySQL中通常作为 CHAR/VARCHAR 处理
                case SqlValueType::CharacterLargeObject:  // MySQL中作为 TEXT 类型处理，绑定时为字符串
                    native_value.data = value.toString(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_VAR_STRING;  // 或更具体的 TEXT 类型
                    break;
                case SqlValueType::ByteArray:
                case SqlValueType::BinaryLargeObject:
                    native_value.data = value.toStdVectorUChar(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_BLOB;
                    break;
                case SqlValueType::Date:
                    {
                        SqlValue::ChronoDate cd = value.toChronoDate(&conversion_successful);
                        if (conversion_successful) {
                            // mysql_protocol::yearMonthDayToMySqlDate 应返回 std::expected<MYSQL_TIME, MySqlProtocolError>
                            auto mt_expected = mysql_protocol::yearMonthDayToMySqlDate(cd);
                            if (mt_expected.has_value()) {
                                native_value.data = mt_expected.value();
                                native_value.original_mysql_type = ::MYSQL_TYPE_DATE;
                            } else {
                                conversion_successful = false;  // 标记协议层转换失败
                            }
                        }
                        break;
                    }
                case SqlValueType::Time:
                    {
                        SqlValue::ChronoTime ct_ns = value.toChronoTime(&conversion_successful);  // SqlValue 返回纳秒
                        if (conversion_successful) {
                            auto ct_us = std::chrono::duration_cast<std::chrono::microseconds>(ct_ns);  // MySQL TIME 通常用微秒
                            auto mt_expected = mysql_protocol::durationToMySqlTime(ct_us);
                            if (mt_expected.has_value()) {
                                native_value.data = mt_expected.value();
                                native_value.original_mysql_type = ::MYSQL_TYPE_TIME;
                            } else {
                                conversion_successful = false;
                            }
                        }
                        break;
                    }
                case SqlValueType::DateTime:
                case SqlValueType::Timestamp:
                    {  // MySQL TIMESTAMP 和 DATETIME 在绑定时通常类似
                        SqlValue::ChronoDateTime cdt = value.toChronoDateTime(&conversion_successful);
                        if (conversion_successful) {
                            auto mt_expected = mysql_protocol::systemClockTimePointToMySqlTime(cdt, ::MYSQL_TYPE_DATETIME);
                            if (mt_expected.has_value()) {
                                native_value.data = mt_expected.value();
                                native_value.original_mysql_type = ::MYSQL_TYPE_DATETIME;
                            } else {
                                conversion_successful = false;
                            }
                        }
                        break;
                    }
                case SqlValueType::Decimal:
                case SqlValueType::Numeric:
                    // MySQL 的 DECIMAL/NUMERIC 类型在 C API 中通常作为字符串进行绑定和检索
                    native_value.data = value.toString(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_NEWDECIMAL;
                    break;
                case SqlValueType::Json:
                    // MySQL JSON 类型在 C API 中作为字符串绑定
                    native_value.data = value.toString(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_JSON;
                    break;
                case SqlValueType::Xml:  // MySQL XML 类型也可能作为字符串处理
                    native_value.data = value.toString(&conversion_successful);
                    native_value.original_mysql_type = ::MYSQL_TYPE_VAR_STRING;  // 或者特定的XML类型（如果MySQL C API支持）
                    break;
                case SqlValueType::Interval:  // MySQL 不直接支持 INTERVAL 类型的绑定，通常构造字符串
                case SqlValueType::Array:     // MySQL 不直接支持 ARRAY 类型的绑定
                case SqlValueType::RowId:     // MySQL 没有通用的 RowId 类型
                case SqlValueType::Custom:
                case SqlValueType::Unknown:
                default:
                    conversion_successful = false;  // 不支持或未知类型
                    // TODO: Log a warning for unsupported SqlValueType
                    break;
            }

            if (!conversion_successful && !value.isNull()) {
                // 如果 SqlValue 非空，但转换失败，则将 native_value 重置为 NULL
                // 这确保我们不会发送一个部分转换或无效的数据
                native_value.data = std::monostate{};
                native_value.original_mysql_type = ::MYSQL_TYPE_NULL;
                native_value.original_mysql_flags = 0;
                // TODO: Log a warning about conversion failure for a non-null SqlValue
            }

            return native_value;
        }

        SqlValue mySqlNativeValueToSqlValue(const mysql_protocol::MySqlNativeValue& nativeValue) {
            if (nativeValue.is_null()) {
                return SqlValue();  // SqlValue 默认构造为 null
            }

            // 使用 std::visit 或一系列的 std::get_if 来处理 std::variant
            if (const bool* val = std::get_if<bool>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const int8_t* val = std::get_if<int8_t>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const uint8_t* val = std::get_if<uint8_t>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const int16_t* val = std::get_if<int16_t>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const uint16_t* val = std::get_if<uint16_t>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const int32_t* val = std::get_if<int32_t>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const uint32_t* val = std::get_if<uint32_t>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const int64_t* val = std::get_if<int64_t>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const uint64_t* val = std::get_if<uint64_t>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const float* val = std::get_if<float>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const double* val = std::get_if<double>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const std::string* val = std::get_if<std::string>(&nativeValue.data)) {
                // 对于字符串，可以根据 nativeValue.original_mysql_type 提供类型提示给 SqlValue
                // 假设 SqlValue 构造函数可以接受 (const std::string&, SqlValueType type_hint)
                // SqlValueType hint = mySqlColumnTypeToSqlValueType(nativeValue.original_mysql_type, nativeValue.original_mysql_flags);
                // return SqlValue(*val, hint);
                return SqlValue(*val);  // 简单版本
            } else if (const std::vector<unsigned char>* val = std::get_if<std::vector<unsigned char>>(&nativeValue.data)) {
                return SqlValue(*val);
            } else if (const MYSQL_TIME* mt_ptr = std::get_if<MYSQL_TIME>(&nativeValue.data)) {
                const MYSQL_TIME& mt = *mt_ptr;
                // 使用 mysql_protocol 中的辅助函数将 MYSQL_TIME 转换为 std::chrono 类型
                switch (mt.time_type) {
                    case MYSQL_TIMESTAMP_DATE:
                        {
                            auto ymd_expected = mysql_protocol::mySqlTimeToYearMonthDay(mt);
                            if (ymd_expected.has_value()) {
                                return SqlValue(ymd_expected.value());
                            }
                            // TODO: Log protocol conversion error: ymd_expected.error().message
                            break;
                        }
                    case MYSQL_TIMESTAMP_TIME:
                        {
                            auto dur_expected = mysql_protocol::mySqlTimeToDuration(mt);  // 返回 std::chrono::microseconds
                            if (dur_expected.has_value()) {
                                // SqlValue 需要能从 std::chrono::microseconds 或 std::chrono::nanoseconds 构造 Time 类型
                                return SqlValue(std::chrono::duration_cast<SqlValue::ChronoTime>(dur_expected.value()));
                            }
                            // TODO: Log protocol conversion error
                            break;
                        }
                    case MYSQL_TIMESTAMP_DATETIME:
                    case MYSQL_TIMESTAMP_DATETIME_TZ:
                        {  // SqlValue 通常不处理时区，依赖 UTC 或本地时间
                            auto tp_expected = mysql_protocol::mySqlTimeToSystemClockTimePoint(mt);
                            if (tp_expected.has_value()) {
                                return SqlValue(tp_expected.value());
                            }
                            // TODO: Log protocol conversion error
                            break;
                        }
                    default:
                        // TODO: Log unhandled MYSQL_TIME type_type
                        break;
                }
            }

            // 如果没有匹配的类型或转换失败
            // TODO: Log a warning: "Failed to convert MySqlNativeValue (original type: "
            //       << nativeValue.original_mysql_type << ") to SqlValue."
            return SqlValue();  // 返回一个表示 null 的 SqlValue
        }

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver