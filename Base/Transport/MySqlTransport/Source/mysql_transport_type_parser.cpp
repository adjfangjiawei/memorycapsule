// cpporm_mysql_transport/mysql_transport_type_parser.cpp
#include "cpporm_mysql_transport/mysql_transport_type_parser.h"  // Header for the function itself

#include <mysql/mysql.h>      // For flags like UNSIGNED_FLAG etc.
#include <mysql/mysql_com.h>  // For MYSQL_TYPE_ enums

#include <algorithm>  // For std::transform, std::remove_if
#include <cctype>     // For ::isspace, ::tolower
#include <string>

#include "cpporm_mysql_transport/mysql_transport_types.h"  // For MySqlTransportFieldMeta

// Helper from the previous version of mysql_transport_column_lister.cpp
// (or a similar helper defined here or in a common utility file)
static std::string removeSubstringCaseInsensitive_parser_local(std::string mainStr, const std::string& subStr) {
    std::string resultStr;
    std::string mainLower = mainStr;
    std::string subLower = subStr;
    std::transform(mainLower.begin(), mainLower.end(), mainLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::transform(subLower.begin(), subLower.end(), subLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    size_t current_pos = 0;
    size_t found_pos = mainLower.find(subLower, current_pos);
    while (found_pos != std::string::npos) {
        resultStr += mainStr.substr(current_pos, found_pos - current_pos);
        current_pos = found_pos + subStr.length();
        found_pos = mainLower.find(subLower, current_pos);
    }
    resultStr += mainStr.substr(current_pos);
    return resultStr;
}

namespace cpporm_mysql_transport {

    bool parseMySQLTypeStringInternal(const std::string& type_str_orig, MySqlTransportFieldMeta& field_meta) {
        if (type_str_orig.empty()) return false;

        std::string working_type_str = type_str_orig;

        std::string lower_check_str = working_type_str;
        std::transform(lower_check_str.begin(), lower_check_str.end(), lower_check_str.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (lower_check_str.find("unsigned") != std::string::npos) {
            field_meta.flags |= UNSIGNED_FLAG;
            working_type_str = removeSubstringCaseInsensitive_parser_local(working_type_str, "unsigned");
        }

        // Re-lower after removing "unsigned" as "unsigned" itself might affect paren parsing if not careful
        lower_check_str = working_type_str;
        std::transform(lower_check_str.begin(), lower_check_str.end(), lower_check_str.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (lower_check_str.find("zerofill") != std::string::npos) {
            field_meta.flags |= ZEROFILL_FLAG;
            working_type_str = removeSubstringCaseInsensitive_parser_local(working_type_str, "zerofill");
        }

        // Trim whitespace from the potentially modified working_type_str
        working_type_str.erase(0, working_type_str.find_first_not_of(" \t\n\r\f\v"));
        working_type_str.erase(working_type_str.find_last_not_of(" \t\n\r\f\v") + 1);

        // Re-lower the trimmed string for base type name extraction
        std::string base_type_name_part_lower = working_type_str;
        std::transform(base_type_name_part_lower.begin(), base_type_name_part_lower.end(), base_type_name_part_lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        size_t paren_open = base_type_name_part_lower.find('(');
        if (paren_open != std::string::npos) {
            size_t paren_close = base_type_name_part_lower.rfind(')');
            if (paren_close != std::string::npos && paren_close > paren_open) {
                std::string params_str = base_type_name_part_lower.substr(paren_open + 1, paren_close - (paren_open + 1));
                // Extract the base type name without parameters and trailing spaces
                base_type_name_part_lower = base_type_name_part_lower.substr(0, paren_open);
                base_type_name_part_lower.erase(base_type_name_part_lower.find_last_not_of(" \t\n\r\f\v") + 1);

                if (base_type_name_part_lower == "tinyint" || base_type_name_part_lower == "smallint" || base_type_name_part_lower == "mediumint" || base_type_name_part_lower == "int" || base_type_name_part_lower == "bigint" || base_type_name_part_lower == "bit") {
                    try {
                        field_meta.length = std::stoul(params_str);
                    } catch (...) { /* ignore parsing errors for length */
                    }
                } else if (base_type_name_part_lower == "char" || base_type_name_part_lower == "varchar" || base_type_name_part_lower == "binary" || base_type_name_part_lower == "varbinary") {
                    try {
                        field_meta.length = std::stoul(params_str);
                    } catch (...) { /* ignore */
                    }
                } else if (base_type_name_part_lower == "float" || base_type_name_part_lower == "double" || base_type_name_part_lower == "real" || base_type_name_part_lower == "decimal" || base_type_name_part_lower == "numeric" || base_type_name_part_lower == "dec") {
                    size_t comma_pos = params_str.find(',');
                    if (comma_pos != std::string::npos) {
                        try {
                            field_meta.length = std::stoul(params_str.substr(0, comma_pos));  // Precision for decimal
                        } catch (...) {                                                       /*ignore*/
                        }
                        try {
                            field_meta.decimals = static_cast<unsigned int>(std::stoul(params_str.substr(comma_pos + 1)));  // Scale for decimal
                        } catch (...) {                                                                                     /*ignore*/
                        }
                    } else {  // Only precision specified for float/double, or M for decimal
                        try {
                            if (base_type_name_part_lower == "decimal" || base_type_name_part_lower == "numeric" || base_type_name_part_lower == "dec") {
                                field_meta.length = std::stoul(params_str);  // M for DECIMAL(M)
                                field_meta.decimals = 0;                     // Default scale 0
                            } else {                                         // float(P) or double(P) - P is bits for storage, not display length. MySQL does not use P for float/double in that way.
                                                                             // For float/double, (M,D) can be specified but M is total digits, D is decimals.
                                                                             // If only one number, it is usually ignored or sets display width for integers with zerofill.
                                                                             // So, just parsing length might be misleading for float/double.
                                                                             // Typically, SHOW COLUMNS returns float/double without (M,D) unless defined with it.
                                                                             // Let's assume if single param for float/double, it's total digits (length).
                                field_meta.length = std::stoul(params_str);
                            }
                        } catch (...) { /*ignore*/
                        }
                    }
                }
                // For ENUM and SET, params_str contains the list of values.
                // Example: ENUM('a','b','c') or SET('x','y','z')
                // field_meta.length could be set to number of options, or max string length of an option.
                // MySqlTransportFieldMeta does not have a dedicated place for enum/set values.
                // This parsing part is complex and depends on how you want to represent it.
                // For native_type_id, just map the base type.
            }
            // If no parentheses, base_type_name_part_lower is already the cleaned base type.
        }

        if (base_type_name_part_lower == "tinyint")
            field_meta.native_type_id = MYSQL_TYPE_TINY;
        else if (base_type_name_part_lower == "smallint")
            field_meta.native_type_id = MYSQL_TYPE_SHORT;
        else if (base_type_name_part_lower == "mediumint")
            field_meta.native_type_id = MYSQL_TYPE_INT24;
        else if (base_type_name_part_lower == "int" || base_type_name_part_lower == "integer")
            field_meta.native_type_id = MYSQL_TYPE_LONG;
        else if (base_type_name_part_lower == "bigint")
            field_meta.native_type_id = MYSQL_TYPE_LONGLONG;
        else if (base_type_name_part_lower == "float")
            field_meta.native_type_id = MYSQL_TYPE_FLOAT;
        else if (base_type_name_part_lower == "double" || base_type_name_part_lower == "real")
            field_meta.native_type_id = MYSQL_TYPE_DOUBLE;
        else if (base_type_name_part_lower == "decimal" || base_type_name_part_lower == "numeric" || base_type_name_part_lower == "dec")
            field_meta.native_type_id = MYSQL_TYPE_NEWDECIMAL;
        else if (base_type_name_part_lower == "date")
            field_meta.native_type_id = MYSQL_TYPE_DATE;
        else if (base_type_name_part_lower == "datetime")
            field_meta.native_type_id = MYSQL_TYPE_DATETIME;
        else if (base_type_name_part_lower == "timestamp")
            field_meta.native_type_id = MYSQL_TYPE_TIMESTAMP;
        else if (base_type_name_part_lower == "time")
            field_meta.native_type_id = MYSQL_TYPE_TIME;
        else if (base_type_name_part_lower == "year")
            field_meta.native_type_id = MYSQL_TYPE_YEAR;
        else if (base_type_name_part_lower == "char") {
            field_meta.native_type_id = MYSQL_TYPE_STRING;
            if (field_meta.length == 0) field_meta.length = 1;
        }  // Default CHAR(1) if no length
        else if (base_type_name_part_lower == "varchar")
            field_meta.native_type_id = MYSQL_TYPE_VAR_STRING;
        else if (base_type_name_part_lower == "tinytext") {
            field_meta.native_type_id = MYSQL_TYPE_TINY_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part_lower == "text") {
            field_meta.native_type_id = MYSQL_TYPE_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part_lower == "mediumtext") {
            field_meta.native_type_id = MYSQL_TYPE_MEDIUM_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part_lower == "longtext") {
            field_meta.native_type_id = MYSQL_TYPE_LONG_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part_lower == "tinyblob") {
            field_meta.native_type_id = MYSQL_TYPE_TINY_BLOB;
            field_meta.flags |= BLOB_FLAG | BINARY_FLAG;
        } else if (base_type_name_part_lower == "blob") {
            field_meta.native_type_id = MYSQL_TYPE_BLOB;
            field_meta.flags |= BLOB_FLAG | BINARY_FLAG;
        } else if (base_type_name_part_lower == "mediumblob") {
            field_meta.native_type_id = MYSQL_TYPE_MEDIUM_BLOB;
            field_meta.flags |= BLOB_FLAG | BINARY_FLAG;
        } else if (base_type_name_part_lower == "longblob") {
            field_meta.native_type_id = MYSQL_TYPE_LONG_BLOB;
            field_meta.flags |= BLOB_FLAG | BINARY_FLAG;
        } else if (base_type_name_part_lower == "binary") {
            field_meta.native_type_id = MYSQL_TYPE_STRING;
            field_meta.flags |= BINARY_FLAG;
            if (field_meta.length == 0) field_meta.length = 1;
        } else if (base_type_name_part_lower == "varbinary") {
            field_meta.native_type_id = MYSQL_TYPE_VAR_STRING;
            field_meta.flags |= BINARY_FLAG;
        } else if (base_type_name_part_lower == "enum") {
            field_meta.native_type_id = MYSQL_TYPE_ENUM;
            field_meta.flags |= ENUM_FLAG;
        } else if (base_type_name_part_lower == "set") {
            field_meta.native_type_id = MYSQL_TYPE_SET;
            field_meta.flags |= SET_FLAG;
        } else if (base_type_name_part_lower == "bit")
            field_meta.native_type_id = MYSQL_TYPE_BIT;
        else if (base_type_name_part_lower == "json")
            field_meta.native_type_id = MYSQL_TYPE_JSON;
        else if (base_type_name_part_lower == "geometry" || base_type_name_part_lower == "point" || base_type_name_part_lower == "linestring" || base_type_name_part_lower == "polygon" || base_type_name_part_lower == "multipoint" || base_type_name_part_lower == "multilinestring" ||
                 base_type_name_part_lower == "multipolygon" || base_type_name_part_lower == "geometrycollection")
            field_meta.native_type_id = MYSQL_TYPE_GEOMETRY;
        else {                                              // Default for unknown types
            field_meta.native_type_id = MYSQL_TYPE_STRING;  // Or MYSQL_TYPE_UNKNOWN, but STRING is safer for SHOW COLUMNS
        }

        return true;
    }

}  // namespace cpporm_mysql_transport