// cpporm_mysql_transport/mysql_transport_column_lister.cpp
#include "cpporm_mysql_transport/mysql_transport_column_lister.h"

#include <mysql/mysql.h>
#include <mysql/mysql_com.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>  // Required for std::vector

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "cpporm_mysql_transport/mysql_transport_type_parser.h"  // For parseMySQLTypeStringInternal
#include "mysql_protocol/mysql_type_converter.h"                 // For MySqlNativeValue

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
        // int idx_comment = result->getFieldIndex("Comment"); // Was unused

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
                } else {
                    continue;
                }
            } else {
                continue;
            }

            col_meta.original_name = col_meta.name;

            std::optional<mysql_protocol::MySqlNativeValue> type_native_val_opt = result->getValue(static_cast<unsigned int>(idx_type));
            if (type_native_val_opt) {
                std::optional<std::string> type_str_opt = type_native_val_opt->get_if<std::string>();
                if (type_str_opt) {
                    // Call the separated parsing function, ensure namespace resolution
                    if (!cpporm_mysql_transport::parseMySQLTypeStringInternal(*type_str_opt, col_meta)) {
                        // Handle parsing error, maybe log or set a specific error
                        // For now, continue and rely on default field_meta values
                    }
                } else {
                    continue;
                }
            } else {
                continue;
            }

            if (idx_collation != -1) {
                auto coll_native_val_opt = result->getValue(static_cast<unsigned int>(idx_collation));
                if (coll_native_val_opt && !coll_native_val_opt->is_null()) {
                    auto coll_str_opt = coll_native_val_opt->get_if<std::string>();
                    if (coll_str_opt) {
                        // const std::string& collation_name = *coll_str_opt;
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
                if (def_val_opt) {
                    col_meta.default_value = std::move(*def_val_opt);
                }
            }

            if (idx_extra != -1) {
                auto extra_native_val_opt = result->getValue(static_cast<unsigned int>(idx_extra));
                if (extra_native_val_opt && !extra_native_val_opt->is_null()) {
                    auto extra_str_opt = extra_native_val_opt->get_if<std::string>();
                    if (extra_str_opt) {
                        const std::string& extra_str = *extra_str_opt;
                        if (extra_str.find("auto_increment") != std::string::npos) {
                            col_meta.flags |= AUTO_INCREMENT_FLAG;
                        }
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