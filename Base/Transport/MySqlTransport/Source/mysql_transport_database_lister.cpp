// cpporm_mysql_transport/mysql_transport_database_lister.cpp
#include "cpporm_mysql_transport/mysql_transport_database_lister.h"

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "mysql_protocol/mysql_type_converter.h"  // For MySqlNativeValue

namespace cpporm_mysql_transport {

    MySqlTransportDatabaseLister::MySqlTransportDatabaseLister(MySqlTransportConnection* connection_context) : m_conn_ctx(connection_context) {
        if (!m_conn_ctx) {
            setError_(MySqlTransportError::Category::InternalError, "DatabaseLister: Null connection context provided.");
        }
    }

    void MySqlTransportDatabaseLister::clearError_() {
        m_last_error = MySqlTransportError();
    }

    void MySqlTransportDatabaseLister::setError_(MySqlTransportError::Category cat, const std::string& msg) {
        m_last_error = MySqlTransportError(cat, msg);
    }

    void MySqlTransportDatabaseLister::setErrorFromConnection_(const std::string& context) {
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

    std::optional<std::vector<std::string>> MySqlTransportDatabaseLister::listDatabases(const std::string& db_name_pattern) {
        if (!m_conn_ctx || !m_conn_ctx->isConnected()) {
            setError_(MySqlTransportError::Category::ConnectionError, "Not connected for listDatabases.");
            return std::nullopt;
        }
        clearError_();

        std::string query = "SHOW DATABASES";
        if (!db_name_pattern.empty()) {
            query += " LIKE '" + m_conn_ctx->escapeString(db_name_pattern, false) + "'";
        }

        std::unique_ptr<MySqlTransportStatement> stmt = m_conn_ctx->createStatement(query);
        if (!stmt) {
            setErrorFromConnection_("Failed to create statement for listDatabases");
            return std::nullopt;
        }
        std::unique_ptr<MySqlTransportResult> result = stmt->executeQuery();
        if (!result || !result->isValid()) {
            m_last_error = stmt->getError();
            return std::nullopt;
        }

        std::vector<std::string> databases;
        while (result->fetchNextRow()) {
            std::optional<mysql_protocol::MySqlNativeValue> db_name_native_val_opt = result->getValue(0);
            if (db_name_native_val_opt) {  // Check if optional<MySqlNativeValue> has a value
                const mysql_protocol::MySqlNativeValue& native_value = *db_name_native_val_opt;
                std::optional<std::string> db_name_str_opt = native_value.get_if<std::string>();
                if (db_name_str_opt) {  // Check if optional<string> has a value
                    databases.push_back(*db_name_str_opt);
                }
            }
        }
        if (!result->getError().isOk()) {
            m_last_error = result->getError();
        }
        return databases;
    }

    MySqlTransportError MySqlTransportDatabaseLister::getLastError() const {
        return m_last_error;
    }

}  // namespace cpporm_mysql_transport