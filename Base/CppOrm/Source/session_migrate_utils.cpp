#include <QString>
#include <algorithm>
#include <cctype>

#include "cpporm/session_migrate_priv.h"

namespace cpporm {
    namespace internal {

        // 规范化数据库类型字符串，以便于比较
        std::string normalizeDbType(const std::string &db_type_raw, const QString &driverNameUpperQ) {
            std::string lower_type = db_type_raw;
            std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            std::string driverNameUpper = driverNameUpperQ.toStdString();

            if (driverNameUpper == "MYSQL" || driverNameUpper == "MARIADB" || driverNameUpper == "QMYSQL" || driverNameUpper == "QMARIADB") {
                if (lower_type.rfind("int", 0) == 0 && lower_type.find("unsigned") == std::string::npos && lower_type != "tinyint(1)") return "int";
                if (lower_type.rfind("int unsigned", 0) == 0) return "int unsigned";
                if (lower_type.rfind("bigint", 0) == 0 && lower_type.find("unsigned") == std::string::npos) return "bigint";
                if (lower_type.rfind("bigint unsigned", 0) == 0) return "bigint unsigned";
                if (lower_type == "tinyint(1)") return "boolean";
                if (lower_type.rfind("varchar", 0) == 0) return "varchar";
                if (lower_type.rfind("char", 0) == 0 && lower_type.find("varchar") == std::string::npos) return "char";
                if (lower_type == "text" || lower_type == "tinytext" || lower_type == "mediumtext" || lower_type == "longtext") return "text";
                if (lower_type == "datetime") return "datetime";
                if (lower_type == "timestamp") return "timestamp";
                if (lower_type == "date") return "date";
                if (lower_type == "time") return "time";
                if (lower_type == "float") return "float";
                if (lower_type == "double" || lower_type == "real") return "double";
                if (lower_type.rfind("decimal", 0) == 0 || lower_type.rfind("numeric", 0) == 0) return "decimal";
                if (lower_type == "blob" || lower_type == "tinyblob" || lower_type == "mediumblob" || lower_type == "longblob" || lower_type == "varbinary" || lower_type == "binary") return "blob";
                if (lower_type == "json") return "json";
                if (lower_type == "point" || lower_type == "geometry" /* etc for spatial types */) return "geometry";
            } else if (driverNameUpper == "QPSQL" || driverNameUpper == "POSTGRESQL") {
                if (lower_type == "integer" || lower_type == "int4") return "int";
                if (lower_type == "bigint" || lower_type == "int8") return "bigint";
                if (lower_type == "smallint" || lower_type == "int2") return "smallint";
                if (lower_type == "boolean" || lower_type == "bool") return "boolean";
                if (lower_type.rfind("character varying", 0) == 0) return "varchar";
                if ((lower_type.rfind("character(", 0) == 0 || lower_type.rfind("char(", 0) == 0) && lower_type.find("varying") == std::string::npos) return "char";
                if (lower_type == "text") return "text";
                if (lower_type == "timestamp without time zone" || lower_type == "timestamp") return "timestamp";
                if (lower_type == "timestamp with time zone") return "timestamptz";
                if (lower_type == "date") return "date";
                if (lower_type == "time without time zone" || lower_type == "time") return "time";
                if (lower_type == "time with time zone") return "timetz";
                if (lower_type == "real" || lower_type == "float4") return "float";
                if (lower_type == "double precision" || lower_type == "float8") return "double";
                if (lower_type == "numeric" || lower_type == "decimal") return "decimal";
                if (lower_type == "bytea") return "blob";
                if (lower_type == "json" || lower_type == "jsonb") return "json";
                if (lower_type == "uuid") return "uuid";
                if (lower_type.find("[]") != std::string::npos) return "array";
            } else if (driverNameUpper == "QSQLITE") {
                if (lower_type.find("int") != std::string::npos) return "int";
                if (lower_type == "text" || lower_type.find("char") != std::string::npos || lower_type.find("clob") != std::string::npos) return "text";
                if (lower_type == "blob" || lower_type.empty()) return "blob";
                if (lower_type == "real" || lower_type.find("floa") != std::string::npos || lower_type.find("doub") != std::string::npos) return "double";
                if (lower_type == "numeric" || lower_type.find("deci") != std::string::npos || lower_type.find("bool") != std::string::npos || lower_type.find("date") != std::string::npos || lower_type.find("datetime") != std::string::npos) return "numeric";
            }
            return lower_type;
        }

    }  // namespace internal
}  // namespace cpporm