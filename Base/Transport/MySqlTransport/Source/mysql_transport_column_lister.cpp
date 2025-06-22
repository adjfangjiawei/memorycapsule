// cpporm_mysql_transport/mysql_transport_column_lister.cpp
#include "cpporm_mysql_transport/mysql_transport_column_lister.h"

#include <mysql/mysql_com.h>  // For MYSQL_TYPE_ enums, if not fully in mysql.h

#include <algorithm>  // For std::transform, std::tolower
#include <sstream>    // For parsing parts of type string

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "mysql_protocol/mysql_type_converter.h"  // For MySqlNativeValue

namespace cpporm_mysql_transport {

    MySqlTransportColumnLister::MySqlTransportColumnLister(MySqlTransportConnection* connection_context) : m_conn_ctx(connection_context) {
        if (!m_conn_ctx) {
            setError_(MySqlTransportError::Category::InternalError, "ColumnLister: Null connection context provided.");
        }
    }

    void MySqlTransportColumnLister::clearError_() {
        m_last_error = MySqlTransportError();
    }

    void MySqlTransportColumnLister::setError_(MySqlTransportError::Category cat, const std::string& msg) {
        m_last_error = MySqlTransportError(cat, msg);
    }

    void MySqlTransportColumnLister::setErrorFromConnection_(const std::string& context) {
        if (m_conn_ctx) {
            m_last_error = m_conn_ctx->getLastError();
            std::string combined_msg = context;
            if (!m_last_error.message.empty()) {
                if (!combined_msg.empty()) combined_msg += ": ";
                combined_msg += m_last_error.message;
            }
            m_last_error.message = combined_msg;
            if (m_last_error.isOk() && !context.empty()) {
                m_last_error.category = MySqlTransportError::Category::InternalError;
            }
        } else {
            setError_(MySqlTransportError::Category::InternalError, context.empty() ? "Lister: Connection context is null." : context + ": Connection context is null.");
        }
    }

    // Placeholder for a more robust MySQL type string parser
    // Example type_str: "int(11)", "varchar(255)", "decimal(10,2) unsigned", "enum('a','b') zerofill"
    bool MySqlTransportColumnLister::parseMySQLTypeString(const std::string& type_str_orig, MySqlTransportFieldMeta& field_meta) const {
        if (type_str_orig.empty()) return false;

        std::string type_str = type_str_orig;
        std::string lower_type_str = type_str;  // Keep original case for some parts if needed later
        std::transform(lower_type_str.begin(), lower_type_str.end(), lower_type_str.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        // Handle attributes first (from lowercased string)
        if (lower_type_str.find("unsigned") != std::string::npos) {
            field_meta.flags |= UNSIGNED_FLAG;
            size_t pos = lower_type_str.find("unsigned");
            // Remove from original-case string if we were to use it, or from a working copy
            // For simplicity, assume attributes are always lowercase or we parse from lower_type_str then reconstruct.
            // This part needs careful handling if mixed case attributes are possible.
            // Assuming SHOW COLUMNS gives lowercase attributes for now.
            type_str.erase(std::remove_if(type_str.begin(), type_str.end(), ::isspace), type_str.end());  // More robust removal
            std::string temp_type_str_no_unsigned;
            size_t current_pos = 0;
            size_t found_pos = type_str.find("unsigned");  // find in potentially mixed case
            while (found_pos != std::string::npos) {
                temp_type_str_no_unsigned += type_str.substr(current_pos, found_pos - current_pos);
                current_pos = found_pos + 8;  // length of "unsigned"
                found_pos = type_str.find("unsigned", current_pos);
            }
            temp_type_str_no_unsigned += type_str.substr(current_pos);
            type_str = temp_type_str_no_unsigned;
        }
        if (lower_type_str.find("zerofill") != std::string::npos) {
            field_meta.flags |= ZEROFILL_FLAG;
            size_t pos = lower_type_str.find("zerofill");

            std::string temp_type_str_no_zerofill;
            size_t current_pos = 0;
            size_t found_pos = type_str.find("zerofill");
            while (found_pos != std::string::npos) {
                temp_type_str_no_zerofill += type_str.substr(current_pos, found_pos - current_pos);
                current_pos = found_pos + 8;  // length of "zerofill"
                found_pos = type_str.find("zerofill", current_pos);
            }
            temp_type_str_no_zerofill += type_str.substr(current_pos);
            type_str = temp_type_str_no_zerofill;
        }
        // Trim whitespace that might be left from original type_str
        type_str.erase(0, type_str.find_first_not_of(" \t\n\r\f\v"));
        type_str.erase(type_str.find_last_not_of(" \t\n\r\f\v") + 1);

        // now work with the cleaned 'type_str' for base type and parameters
        std::string base_type_lower = type_str;  // start with the full cleaned string
        std::transform(base_type_lower.begin(), base_type_lower.end(), base_type_lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        std::string base_type_name_part;
        size_t paren_open = base_type_lower.find('(');
        size_t paren_close = base_type_lower.find(')');

        if (paren_open != std::string::npos && paren_close != std::string::npos && paren_close > paren_open) {
            base_type_name_part = base_type_lower.substr(0, paren_open);
            std::string params_str = base_type_lower.substr(paren_open + 1, paren_close - (paren_open + 1));

            if (base_type_name_part == "tinyint" || base_type_name_part == "smallint" || base_type_name_part == "mediumint" || base_type_name_part == "int" || base_type_name_part == "bigint" || base_type_name_part == "bit") {
                try {
                    field_meta.length = std::stoul(params_str);
                } catch (...) { /* ignore parse error for length */
                }
            } else if (base_type_name_part == "char" || base_type_name_part == "varchar" || base_type_name_part == "binary" || base_type_name_part == "varbinary") {
                try {
                    field_meta.length = std::stoul(params_str);
                } catch (...) { /* ignore */
                }
            } else if (base_type_name_part == "float" || base_type_name_part == "double" || base_type_name_part == "real" || base_type_name_part == "decimal" || base_type_name_part == "numeric") {
                size_t comma_pos = params_str.find(',');
                if (comma_pos != std::string::npos) {
                    try {
                        field_meta.length = std::stoul(params_str.substr(0, comma_pos));  // This is precision for decimal
                    } catch (...) {                                                       /*ignore*/
                    }
                    try {
                        field_meta.decimals = std::stoul(params_str.substr(comma_pos + 1));  // This is scale for decimal
                    } catch (...) {                                                          /*ignore*/
                    }
                } else {  // Only M specified (precision for float/double, or just length)
                    try {
                        // For float(M,D) and double(M,D), M is total digits, D is after decimal.
                        // If only float(P) where P <= 24, it's a float. If P > 24, it's a double.
                        // MySQL's length for float/double in SHOW COLUMNS type string (e.g., float(P,D))
                        // is not directly the storage bytes but display parameters.
                        // We are more interested in mapping to MYSQL_TYPE_FLOAT/DOUBLE.
                        // The `length` field in MySqlTransportFieldMeta might be ambiguous here.
                        // Let's assume stoul gives some numeric value for display length if only one param.
                        if (base_type_name_part == "decimal" || base_type_name_part == "numeric") {
                            field_meta.length = std::stoul(params_str);  // Precision
                            field_meta.decimals = 0;                     // Default scale if not specified
                        } else {
                            // For float/double, if only one number is given, it's not clearly length or precision.
                            // Let's not populate field_meta.length or decimals from this single param for float/double to avoid confusion.
                        }
                    } catch (...) { /*ignore*/
                    }
                }
            }
        } else {
            base_type_name_part = base_type_lower;
        }

        // Map base_type_name_part string to enum enum_field_types
        if (base_type_name_part == "tinyint")
            field_meta.native_type_id = MYSQL_TYPE_TINY;
        else if (base_type_name_part == "smallint")
            field_meta.native_type_id = MYSQL_TYPE_SHORT;
        else if (base_type_name_part == "mediumint")
            field_meta.native_type_id = MYSQL_TYPE_INT24;
        else if (base_type_name_part == "int" || base_type_name_part == "integer")
            field_meta.native_type_id = MYSQL_TYPE_LONG;
        else if (base_type_name_part == "bigint")
            field_meta.native_type_id = MYSQL_TYPE_LONGLONG;
        else if (base_type_name_part == "float")
            field_meta.native_type_id = MYSQL_TYPE_FLOAT;
        else if (base_type_name_part == "double" || base_type_name_part == "real")
            field_meta.native_type_id = MYSQL_TYPE_DOUBLE;
        else if (base_type_name_part == "decimal" || base_type_name_part == "numeric" || base_type_name_part == "dec")
            field_meta.native_type_id = MYSQL_TYPE_NEWDECIMAL;
        else if (base_type_name_part == "date")
            field_meta.native_type_id = MYSQL_TYPE_DATE;
        else if (base_type_name_part == "datetime")
            field_meta.native_type_id = MYSQL_TYPE_DATETIME;
        else if (base_type_name_part == "timestamp")
            field_meta.native_type_id = MYSQL_TYPE_TIMESTAMP;
        else if (base_type_name_part == "time")
            field_meta.native_type_id = MYSQL_TYPE_TIME;
        else if (base_type_name_part == "year")
            field_meta.native_type_id = MYSQL_TYPE_YEAR;
        else if (base_type_name_part == "char")
            field_meta.native_type_id = MYSQL_TYPE_STRING;  // For CHAR
        else if (base_type_name_part == "varchar")
            field_meta.native_type_id = MYSQL_TYPE_VAR_STRING;
        else if (base_type_name_part == "tinytext") {
            field_meta.native_type_id = MYSQL_TYPE_TINY_BLOB;  // MySQL uses BLOB types for TEXT types internally
            field_meta.flags |= BLOB_FLAG;                     // This flag is often set for TEXT types too
        } else if (base_type_name_part == "text") {
            field_meta.native_type_id = MYSQL_TYPE_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part == "mediumtext") {
            field_meta.native_type_id = MYSQL_TYPE_MEDIUM_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part == "longtext") {
            field_meta.native_type_id = MYSQL_TYPE_LONG_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part == "tinyblob") {
            field_meta.native_type_id = MYSQL_TYPE_TINY_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part == "blob") {
            field_meta.native_type_id = MYSQL_TYPE_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part == "mediumblob") {
            field_meta.native_type_id = MYSQL_TYPE_MEDIUM_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part == "longblob") {
            field_meta.native_type_id = MYSQL_TYPE_LONG_BLOB;
            field_meta.flags |= BLOB_FLAG;
        } else if (base_type_name_part == "binary")
            field_meta.native_type_id = MYSQL_TYPE_STRING;  // For BINARY, represented as string but fixed length and binary collation
        else if (base_type_name_part == "varbinary")
            field_meta.native_type_id = MYSQL_TYPE_VAR_STRING;  // For VARBINARY
        else if (base_type_name_part == "enum") {
            field_meta.native_type_id = MYSQL_TYPE_ENUM;
            field_meta.flags |= ENUM_FLAG;
        } else if (base_type_name_part == "set") {
            field_meta.native_type_id = MYSQL_TYPE_SET;
            field_meta.flags |= SET_FLAG;
        } else if (base_type_name_part == "bit")
            field_meta.native_type_id = MYSQL_TYPE_BIT;
        else if (base_type_name_part == "json")
            field_meta.native_type_id = MYSQL_TYPE_JSON;
        else if (base_type_name_part == "geometry" || base_type_name_part == "point" || base_type_name_part == "linestring" || base_type_name_part == "polygon" || base_type_name_part == "multipoint" || base_type_name_part == "multilinestring" || base_type_name_part == "multipolygon" ||
                 base_type_name_part == "geometrycollection")
            field_meta.native_type_id = MYSQL_TYPE_GEOMETRY;
        else {
            field_meta.native_type_id = MYSQL_TYPE_STRING;  // Fallback for unknown types
        }

        return true;
    }

    std::optional<std::vector<MySqlTransportFieldMeta>> MySqlTransportColumnLister::getTableColumns(const std::string& table_name, const std::string& db_name_filter_param) {
        if (!m_conn_ctx || !m_conn_ctx->isConnected()) {
            setError_(MySqlTransportError::Category::ConnectionError, "Not connected for getTableColumns.");
            return std::nullopt;
        }
        if (table_name.empty()) {
            setError_(MySqlTransportError::Category::ApiUsageError, "Table name cannot be empty for getTableColumns.");
            return std::nullopt;
        }
        clearError_();

        std::string db_to_use = db_name_filter_param;
        if (db_to_use.empty()) {
            db_to_use = m_conn_ctx->getCurrentParams().db_name;
            if (db_to_use.empty()) {
                setError_(MySqlTransportError::Category::ApiUsageError, "Database name not specified for getTableColumns.");
                return std::nullopt;
            }
        }

        std::string fq_table_name = "`" + m_conn_ctx->escapeString(db_to_use, false) + "`.`" + m_conn_ctx->escapeString(table_name, false) + "`";

        std::string query = "SHOW FULL COLUMNS FROM " + fq_table_name;
        std::unique_ptr<MySqlTransportStatement> stmt = m_conn_ctx->createStatement(query);
        if (!stmt) {
            setErrorFromConnection_("Failed to create statement for getTableColumns for " + fq_table_name);
            return std::nullopt;
        }
        std::unique_ptr<MySqlTransportResult> result = stmt->executeQuery();
        if (!result || !result->isValid()) {
            m_last_error = stmt->getError();
            return std::nullopt;
        }

        std::vector<MySqlTransportFieldMeta> columns_meta_vec;

        int idx_field = result->getFieldIndex("Field");
        int idx_type = result->getFieldIndex("Type");
        int idx_collation = result->getFieldIndex("Collation");
        int idx_null = result->getFieldIndex("Null");
        int idx_key = result->getFieldIndex("Key");
        int idx_default = result->getFieldIndex("Default");
        int idx_extra = result->getFieldIndex("Extra");
        int idx_comment = result->getFieldIndex("Comment");

        if (idx_field == -1 || idx_type == -1) {
            setError_(MySqlTransportError::Category::InternalError, "Could not find 'Field' or 'Type' columns in SHOW FULL COLUMNS output.");
            return std::nullopt;
        }

        while (result->fetchNextRow()) {
            MySqlTransportFieldMeta col_meta;

            std::optional<mysql_protocol::MySqlNativeValue> field_native_val_opt = result->getValue(static_cast<unsigned int>(idx_field));
            if (field_native_val_opt) {
                std::optional<std::string> field_str_opt = field_native_val_opt->get_if<std::string>();
                if (field_str_opt) {
                    col_meta.name = *field_str_opt;
                } else
                    continue;
            } else
                continue;

            col_meta.original_name = col_meta.name;

            std::optional<mysql_protocol::MySqlNativeValue> type_native_val_opt = result->getValue(static_cast<unsigned int>(idx_type));
            if (type_native_val_opt) {
                std::optional<std::string> type_str_opt = type_native_val_opt->get_if<std::string>();
                if (type_str_opt) {
                    parseMySQLTypeString(*type_str_opt, col_meta);
                } else
                    continue;
            } else
                continue;

            if (idx_collation != -1) {
                auto coll_native_val_opt = result->getValue(static_cast<unsigned int>(idx_collation));
                if (coll_native_val_opt && !coll_native_val_opt->is_null()) {
                    auto coll_str_opt = coll_native_val_opt->get_if<std::string>();
                    if (coll_str_opt) {
                        // const std::string& collation_name = *coll_str_opt; // Use collation_name
                        // TODO: Get charsetnr from collation name.
                        // This is complex. For now, leave as 0.
                        // A proper solution might involve querying information_schema.COLLATIONS
                        // or using mysql_get_character_set_info with the connection's charset
                        // if the column charset matches the connection charset.
                    }
                }
            }

            if (idx_null != -1) {
                auto null_native_val_opt = result->getValue(static_cast<unsigned int>(idx_null));
                if (null_native_val_opt) {
                    auto null_str_opt = null_native_val_opt->get_if<std::string>();
                    if (null_str_opt && *null_str_opt == "NO") {
                        col_meta.flags |= NOT_NULL_FLAG;
                    }
                }
            }

            if (idx_key != -1) {
                auto key_native_val_opt = result->getValue(static_cast<unsigned int>(idx_key));
                if (key_native_val_opt && !key_native_val_opt->is_null()) {
                    auto key_str_opt = key_native_val_opt->get_if<std::string>();
                    if (key_str_opt) {
                        const std::string& key_str = *key_str_opt;
                        if (key_str == "PRI")
                            col_meta.flags |= PRI_KEY_FLAG;
                        else if (key_str == "UNI")
                            col_meta.flags |= UNIQUE_KEY_FLAG;
                        else if (key_str == "MUL")
                            col_meta.flags |= MULTIPLE_KEY_FLAG;
                    }
                }
            }

            if (idx_default != -1) {
                auto def_val_opt = result->getValue(static_cast<unsigned int>(idx_default));
                if (def_val_opt) {  // getValue returns optional, check if it has value
                    col_meta.default_value = std::move(*def_val_opt);
                }
            }

            if (idx_extra != -1) {
                auto extra_native_val_opt = result->getValue(static_cast<unsigned int>(idx_extra));
                if (extra_native_val_opt && !extra_native_val_opt->is_null()) {
                    auto extra_str_opt = extra_native_val_opt->get_if<std::string>();
                    if (extra_str_opt) {
                        const std::string& extra_str = *extra_str_opt;
                        if (extra_str.find("auto_increment") != std::string::npos) col_meta.flags |= AUTO_INCREMENT_FLAG;
                    }
                }
            }
            col_meta.table = table_name;
            col_meta.db = db_to_use;

            columns_meta_vec.push_back(col_meta);
        }
        if (!result->getError().isOk()) {
            m_last_error = result->getError();
        }
        return columns_meta_vec;
    }

    MySqlTransportError MySqlTransportColumnLister::getLastError() const {
        return m_last_error;
    }

}  // namespace cpporm_mysql_transport