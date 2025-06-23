#include <mysql/mysql.h>

#include <algorithm>  // For std::transform
#include <string>

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"

namespace cpporm_mysql_transport {

    MySqlTransportStatement::MySqlTransportStatement(MySqlTransportConnection* conn, const std::string& query) : m_connection(conn), m_original_query(query), m_stmt_handle(nullptr), m_is_prepared(false), m_is_utility_command(false), m_affected_rows(0), m_last_insert_id(0), m_warning_count(0) {
        if (!m_connection || !m_connection->getNativeHandle()) {
            setError(MySqlTransportError::Category::ApiUsageError, "Invalid or uninitialized connection provided to statement.");
            return;
        }

        std::string upper_query;
        upper_query.resize(m_original_query.size());
        std::transform(m_original_query.begin(), m_original_query.end(), upper_query.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });

        if (upper_query.rfind("SHOW ", 0) == 0 || upper_query.rfind("DESC ", 0) == 0 || upper_query.rfind("DESCRIBE ", 0) == 0 || upper_query.rfind("EXPLAIN ", 0) == 0) {
            m_is_utility_command = true;
        } else {
            m_is_utility_command = false;
            m_stmt_handle = mysql_stmt_init(m_connection->getNativeHandle());
            if (!m_stmt_handle) {
                setErrorFromConnectionHandle(m_connection->getNativeHandle(), "mysql_stmt_init failed");
            }
        }
    }

    MySqlTransportStatement::~MySqlTransportStatement() {
        close();
    }

    MySqlTransportStatement::MySqlTransportStatement(MySqlTransportStatement&& other) noexcept
        : m_connection(other.m_connection),
          m_original_query(std::move(other.m_original_query)),
          m_stmt_handle(other.m_stmt_handle),
          m_is_prepared(other.m_is_prepared),
          m_is_utility_command(other.m_is_utility_command),
          m_bind_buffers(std::move(other.m_bind_buffers)),
          m_param_data_buffers(std::move(other.m_param_data_buffers)),
          m_param_is_null_indicators(std::move(other.m_param_is_null_indicators)),
          m_param_length_indicators(std::move(other.m_param_length_indicators)),
          m_last_error(std::move(other.m_last_error)),
          m_affected_rows(other.m_affected_rows),
          m_last_insert_id(other.m_last_insert_id),
          m_warning_count(other.m_warning_count) {
        other.m_stmt_handle = nullptr;
        other.m_is_prepared = false;
    }

    MySqlTransportStatement& MySqlTransportStatement::operator=(MySqlTransportStatement&& other) noexcept {
        if (this != &other) {
            close();

            m_connection = other.m_connection;
            m_original_query = std::move(other.m_original_query);
            m_stmt_handle = other.m_stmt_handle;
            m_is_prepared = other.m_is_prepared;
            m_is_utility_command = other.m_is_utility_command;
            m_bind_buffers = std::move(other.m_bind_buffers);
            m_param_data_buffers = std::move(other.m_param_data_buffers);
            m_param_is_null_indicators = std::move(other.m_param_is_null_indicators);
            m_param_length_indicators = std::move(other.m_param_length_indicators);
            m_last_error = std::move(other.m_last_error);
            m_affected_rows = other.m_affected_rows;
            m_last_insert_id = other.m_last_insert_id;
            m_warning_count = other.m_warning_count;

            other.m_stmt_handle = nullptr;
            other.m_is_prepared = false;
        }
        return *this;
    }

    void MySqlTransportStatement::close() {
        if (m_stmt_handle) {
            mysql_stmt_close(m_stmt_handle);
            m_stmt_handle = nullptr;
        }
        m_is_prepared = false;
        m_bind_buffers.clear();
        m_param_data_buffers.clear();
        m_param_is_null_indicators.clear();
        m_param_length_indicators.clear();
        clearError();
    }

    bool MySqlTransportStatement::isPrepared() const {
        return m_is_prepared;
    }

    void MySqlTransportStatement::clearError() {
        m_last_error = MySqlTransportError();
    }

    void MySqlTransportStatement::setError(MySqlTransportError::Category cat, const std::string& msg, unsigned int proto_errc) {
        m_last_error = MySqlTransportError(cat, msg, 0, nullptr, nullptr, proto_errc, m_original_query);
    }

    void MySqlTransportStatement::setErrorFromConnectionHandle(MYSQL* handle_to_check_error_on, const std::string& context) {
        if (!handle_to_check_error_on) {
            m_last_error = MySqlTransportError(MySqlTransportError::Category::InternalError, context + ": MySQL connection handle is null.", 0, nullptr, nullptr, 0, m_original_query);
            return;
        }
        unsigned int err_no = mysql_errno(handle_to_check_error_on);
        if (err_no != 0) {
            const char* sql_state = mysql_sqlstate(handle_to_check_error_on);
            const char* err_msg_c = mysql_error(handle_to_check_error_on);
            std::string full_msg = context + ": " + (err_msg_c ? err_msg_c : "Unknown MySQL error");
            m_last_error = MySqlTransportError(MySqlTransportError::Category::QueryError, full_msg, static_cast<int>(err_no), sql_state, err_msg_c, 0, m_original_query);
        } else if (!context.empty() && m_last_error.isOk()) {
            m_last_error = MySqlTransportError(MySqlTransportError::Category::InternalError, context, 0, nullptr, nullptr, 0, m_original_query);
        }
    }

    void MySqlTransportStatement::setErrorFromStatementHandle(const std::string& context) {
        if (!m_stmt_handle) {
            m_last_error = MySqlTransportError(MySqlTransportError::Category::InternalError, context + ": MySQL statement handle is null.", 0, nullptr, nullptr, 0, m_original_query);
            return;
        }
        unsigned int err_no = mysql_stmt_errno(m_stmt_handle);
        if (err_no != 0) {
            const char* sql_state = mysql_stmt_sqlstate(m_stmt_handle);
            const char* err_msg_c = mysql_stmt_error(m_stmt_handle);
            std::string full_msg = context + ": " + (err_msg_c ? err_msg_c : "Unknown MySQL statement error");
            m_last_error = MySqlTransportError(MySqlTransportError::Category::QueryError, full_msg, static_cast<int>(err_no), sql_state, err_msg_c, 0, m_original_query);
        } else if (!context.empty() && m_last_error.isOk()) {
            m_last_error = MySqlTransportError(MySqlTransportError::Category::InternalError, context, 0, nullptr, nullptr, 0, m_original_query);
        }
    }

    void MySqlTransportStatement::setErrorFromProtocol(const mysql_protocol::MySqlProtocolError& proto_err, const std::string& context) {
        m_last_error = MySqlTransportError(MySqlTransportError::Category::ProtocolError, context + ": " + proto_err.error_message, 0, proto_err.sql_state, nullptr, proto_err.error_code, m_original_query);
    }

}  // namespace cpporm_mysql_transport