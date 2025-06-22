#include <mysql/mysql.h>  // For MYSQL_TYPE_* constants and UNSIGNED_FLAG, BINARY_FLAG etc.

#include "sqldriver/mysql/mysql_driver_helper.h"

namespace cpporm_sqldriver {
    namespace mysql_helper {

        SqlValueType mySqlColumnTypeToSqlValueType(int mysql_col_type_id, unsigned int mysql_flags) {
            // mysql_col_type_id 是来自 MYSQL_FIELD::type (enum_field_types)
            // mysql_flags 是来自 MYSQL_FIELD::flags
            switch (mysql_col_type_id) {
                case MYSQL_TYPE_DECIMAL:
                case MYSQL_TYPE_NEWDECIMAL:
                    return SqlValueType::Decimal;  // SqlValue 应能处理Decimal，可能作为字符串或特定类型

                case MYSQL_TYPE_TINY:
                    return (mysql_flags & UNSIGNED_FLAG) ? SqlValueType::UInt8 : SqlValueType::Int8;

                case MYSQL_TYPE_SHORT:
                    return (mysql_flags & UNSIGNED_FLAG) ? SqlValueType::UInt16 : SqlValueType::Int16;

                case MYSQL_TYPE_LONG:  // 通常是32位整数
                    return (mysql_flags & UNSIGNED_FLAG) ? SqlValueType::UInt32 : SqlValueType::Int32;

                case MYSQL_TYPE_FLOAT:
                    return SqlValueType::Float;

                case MYSQL_TYPE_DOUBLE:
                    return SqlValueType::Double;

                case MYSQL_TYPE_NULL:  // 通常表示结果集中的 NULL 值，而不是列本身的类型
                    return SqlValueType::Null;

                case MYSQL_TYPE_TIMESTAMP:          // 通常映射到 DateTime
                    return SqlValueType::DateTime;  // 或者 Timestamp，取决于 SqlValue 的语义

                case MYSQL_TYPE_LONGLONG:  // 64位整数
                    return (mysql_flags & UNSIGNED_FLAG) ? SqlValueType::UInt64 : SqlValueType::Int64;

                case MYSQL_TYPE_INT24:  // 中等整数，通常也作为32位整数处理
                    return (mysql_flags & UNSIGNED_FLAG) ? SqlValueType::UInt32 : SqlValueType::Int32;

                case MYSQL_TYPE_DATE:
                case MYSQL_TYPE_NEWDATE:  // 内部类型，通常作为 DATE 返回
                    return SqlValueType::Date;

                case MYSQL_TYPE_TIME:
                    return SqlValueType::Time;

                case MYSQL_TYPE_DATETIME:
                    return SqlValueType::DateTime;

                case MYSQL_TYPE_YEAR:            // YEAR 类型可以存为 smallint
                    return SqlValueType::Int16;  // 或 UInt16，取决于 SqlValue 的偏好

                case MYSQL_TYPE_VARCHAR:
                case MYSQL_TYPE_VAR_STRING:  // 旧的 VARCHAR
                    return SqlValueType::String;

                case MYSQL_TYPE_BIT:
                    // BIT(1) 通常用于布尔值
                    // BIT(M) 当 M > 1 时，通常作为二进制数据或整数处理
                    // 假设 MySqlTransportFieldMeta::length 存储了 M
                    // 如果长度为1，可以映射为 Bool。否则，可能是 UInt64 或 ByteArray。
                    // 这里简单处理为 UInt64，更精确的需要 transportMeta.length
                    return SqlValueType::UInt64;  // 或 ByteArray，或 Bool

                case MYSQL_TYPE_JSON:
                    return SqlValueType::Json;  // SqlValue 需要能处理JSON，可能通过字符串

                case MYSQL_TYPE_ENUM:
                case MYSQL_TYPE_SET:
                    return SqlValueType::String;  // ENUM 和 SET 通常作为字符串返回

                case MYSQL_TYPE_TINY_BLOB:
                case MYSQL_TYPE_MEDIUM_BLOB:
                case MYSQL_TYPE_LONG_BLOB:
                case MYSQL_TYPE_BLOB:
                    // 需要区分 TEXT 类型 (CharacterLargeObject) 和真正的 BLOB (BinaryLargeObject)
                    // 如果 flags 中没有 BINARY_FLAG，且字符集不是 binary，则可能是 TEXT 类的
                    // 这是一个启发式方法，更准确的判断可能需要字符集信息 (MYSQL_FIELD::charsetnr)
                    if (mysql_flags & BINARY_FLAG) {             // 如果明确是二进制
                        return SqlValueType::BinaryLargeObject;  // 或 ByteArray
                    } else {
                        // 检查是否是 TEXT 变体 (TINYTEXT, TEXT, MEDIUMTEXT, LONGTEXT)
                        // 这些类型在元数据中 flags 可能没有 BINARY_FLAG，但有 BLOB_FLAG
                        // 并且它们的字符集不是 binary collation
                        // 这里简化：如果不是明确的 BINARY，且是 BLOB 类，则可能是 CLOB
                        // TODO: 这里的区分需要更精确的逻辑，可能依赖 MySqlTransportFieldMeta::charsetnr
                        bool is_text_heuristic =
                            ((mysql_col_type_id == MYSQL_TYPE_TINY_BLOB && (mysql_flags & BLOB_FLAG) && !(mysql_flags & BINARY_FLAG) /* && charsetnr is not binary */) || (mysql_col_type_id == MYSQL_TYPE_BLOB && (mysql_flags & BLOB_FLAG) && !(mysql_flags & BINARY_FLAG) /* && ... */) ||
                             (mysql_col_type_id == MYSQL_TYPE_MEDIUM_BLOB && (mysql_flags & BLOB_FLAG) && !(mysql_flags & BINARY_FLAG) /* && ... */) || (mysql_col_type_id == MYSQL_TYPE_LONG_BLOB && (mysql_flags & BLOB_FLAG) && !(mysql_flags & BINARY_FLAG) /* && ... */));
                        if (is_text_heuristic) {  // 这是一个粗略的判断
                            return SqlValueType::CharacterLargeObject;
                        }
                        return SqlValueType::BinaryLargeObject;  // 默认是二进制大对象
                    }

                case MYSQL_TYPE_STRING:                    // 包括 CHAR, BINARY
                    if (mysql_flags & BINARY_FLAG) {       // BINARY(M)
                        return SqlValueType::ByteArray;    // 或者 FixedString 如果需要区分
                    } else {                               // CHAR(M)
                        return SqlValueType::FixedString;  // 或 String
                    }

                case MYSQL_TYPE_GEOMETRY:
                    return SqlValueType::ByteArray;  // Geometry 类型通常作为 WKB (二进制) 返回

                default:
                    return SqlValueType::Unknown;  // 未知或不直接映射的 MySQL 类型
            }
        }

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver