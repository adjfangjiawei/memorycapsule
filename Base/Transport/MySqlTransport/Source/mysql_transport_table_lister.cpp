// cpporm_mysql_transport/mysql_transport_table_lister.cpp
#include "cpporm_mysql_transport/mysql_transport_table_lister.h"

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "mysql_protocol/mysql_type_converter.h"  // For MySqlNativeValue

namespace cpporm_mysql_transport {

    MySqlTransportTableLister::MySqlTransportTableLister(MySqlTransportConnection* connection_context) : m_conn_ctx(connection_context) {
        if (!m_conn_ctx) {
            setError_(MySqlTransportError::Category::InternalError, "TableLister: Null connection context provided.");
        }
    }

    void MySqlTransportTableLister::clearError_() {
        m_last_error = MySqlTransportError();
    }

    void MySqlTransportTableLister::setError_(MySqlTransportError::Category cat, const std::string& msg) {
        m_last_error = MySqlTransportError(cat, msg);
    }

    void MySqlTransportTableLister::setErrorFromConnection_(const std::string& context) {
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

    std::optional<std::vector<std::string>> MySqlTransportTableLister::listShowFullTablesFiltered(const std::string& db_name_filter, const std::string& name_pattern, const std::string& target_table_type) {
        if (!m_conn_ctx || !m_conn_ctx->isConnected()) {
            setError_(MySqlTransportError::Category::ConnectionError, "Not connected for listing " + target_table_type + "s.");
            return std::nullopt;
        }
        clearError_();

        std::string current_db_for_query = db_name_filter;
        if (current_db_for_query.empty()) {
            current_db_for_query = m_conn_ctx->getCurrentParams().db_name;
        }

        std::string query;
        bool use_like_in_query = false;

        if (target_table_type.empty() && !name_pattern.empty()) {
            query = "SHOW TABLES";
            if (!current_db_for_query.empty()) {
                query += " FROM `" + m_conn_ctx->escapeString(current_db_for_query, false) + "`";
            }
            query += " LIKE '" + m_conn_ctx->escapeString(name_pattern, false) + "'";
            use_like_in_query = true;
        } else {
            query = "SHOW FULL TABLES";
            if (!current_db_for_query.empty()) {
                query += " FROM `" + m_conn_ctx->escapeString(current_db_for_query, false) + "`";
            }
            if (!target_table_type.empty()) {
                query += " WHERE `Table_type` = '" + m_conn_ctx->escapeString(target_table_type, false) + "'";
            }
        }

        std::unique_ptr<MySqlTransportStatement> stmt = m_conn_ctx->createStatement(query);
        if (!stmt) {
            setErrorFromConnection_("Failed to create statement for listing " + target_table_type);
            return std::nullopt;
        }
        std::unique_ptr<MySqlTransportResult> result = stmt->executeQuery();
        if (!result || !result->isValid()) {
            m_last_error = stmt->getError();
            return std::nullopt;
        }

        std::vector<std::string> names;
        unsigned int name_col_idx = 0;
        unsigned int type_col_idx = (query.find("SHOW FULL TABLES") != std::string::npos) ? 1 : static_cast<unsigned int>(-1);

        while (result->fetchNextRow()) {
            std::optional<mysql_protocol::MySqlNativeValue> name_native_val_opt = result->getValue(name_col_idx);
            if (name_native_val_opt) {
                const auto& native_val = *name_native_val_opt;
                std::optional<std::string> name_str_opt = native_val.get_if<std::string>();
                if (name_str_opt) {
                    const std::string& current_name = *name_str_opt;

                    bool type_match = target_table_type.empty();
                    if (!type_match && type_col_idx != static_cast<unsigned int>(-1)) {
                        std::optional<mysql_protocol::MySqlNativeValue> type_native_val_opt = result->getValue(type_col_idx);
                        if (type_native_val_opt) {
                            std::optional<std::string> type_str_opt = type_native_val_opt->get_if<std::string>();
                            if (type_str_opt && *type_str_opt == target_table_type) {
                                type_match = true;
                            }
                        }
                    }

                    bool name_pattern_match = name_pattern.empty();
                    if (!name_pattern_match) {
                        if (use_like_in_query) {  // LIKE was used in query
                            name_pattern_match = true;
                        } else {  // Client side filtering for name_pattern
                            // TODO: Implement proper SQL LIKE pattern matching for client-side.
                            // For now, a simple exact match or substring if wildcards are not SQL standard.
                            // This is a placeholder for a more robust LIKE comparison.
                            if (name_pattern.find('%') == std::string::npos && name_pattern.find('_') == std::string::npos) {
                                if (current_name == name_pattern) name_pattern_match = true;
                            } else {
                                // Simplified: assume if pattern exists and not used in query, we need a real LIKE.
                                // For this example, we'll just say it matches if not empty.
                                // In a real scenario, use a regex or fnmatch equivalent.
                                name_pattern_match = (current_name.find(name_pattern.substr(0, name_pattern.find('%'))) != std::string::npos);  // Very basic
                            }
                        }
                    }

                    if (type_match && name_pattern_match) {
                        names.push_back(current_name);
                    }
                }
            }
        }
        if (!result->getError().isOk()) {
            m_last_error = result->getError();
        }
        return names;
    }

    std::optional<std::vector<std::string>> MySqlTransportTableLister::listTables(const std::string& db_name_filter, const std::string& table_name_pattern) {
        return listShowFullTablesFiltered(db_name_filter, table_name_pattern, "BASE TABLE");
    }

    std::optional<std::vector<std::string>> MySqlTransportTableLister::listViews(const std::string& db_name_filter, const std::string& view_name_pattern) {
        return listShowFullTablesFiltered(db_name_filter, view_name_pattern, "VIEW");
    }

    MySqlTransportError MySqlTransportTableLister::getLastError() const {
        return m_last_error;
    }

}  // namespace cpporm_mysql_transport