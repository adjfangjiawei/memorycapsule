// cpporm_mysql_transport/mysql_transport_statement_core.cpp
#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"

namespace cpporm_mysql_transport {

    MySqlTransportStatement::MySqlTransportStatement(MySqlTransportConnection* conn, const std::string& query) : m_connection(conn), m_original_query(query), m_stmt_handle(nullptr), m_is_prepared(false), m_affected_rows(0), m_last_insert_id(0), m_warning_count(0) {
        if (!m_connection || !m_connection->getNativeHandle()) {
            setError(MySqlTransportError::Category::ApiUsageError, "Invalid or uninitialized connection provided to statement.");
            return;
        }

        m_stmt_handle = mysql_stmt_init(m_connection->getNativeHandle());
        if (!m_stmt_handle) {
            setErrorFromMySQL();
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

    // getNativeStatementHandle() is defined inline in the header, remove from .cpp
    // MYSQL_STMT* MySqlTransportStatement::getNativeStatementHandle() const {
    //     return m_stmt_handle;
    // }

    // getConnection() is defined inline in the header, remove from .cpp
    // MySqlTransportConnection* MySqlTransportStatement::getConnection() const {
    //     return m_connection;
    // }

    void MySqlTransportStatement::clearError() {
        m_last_error = MySqlTransportError();
    }

    void MySqlTransportStatement::setError(MySqlTransportError::Category cat, const std::string& msg, unsigned int proto_errc) {
        m_last_error = MySqlTransportError(cat, msg, 0, nullptr, nullptr, proto_errc, m_original_query);
    }

    void MySqlTransportStatement::setErrorFromMySQL() {
        unsigned int err_no = 0;
        const char* sql_state = nullptr;
        const char* err_msg = nullptr;

        if (m_stmt_handle) {
            err_no = mysql_stmt_errno(m_stmt_handle);
            sql_state = mysql_stmt_sqlstate(m_stmt_handle);
            err_msg = mysql_stmt_error(m_stmt_handle);
        } else if (m_connection && m_connection->getNativeHandle()) {
            MYSQL* conn_handle = m_connection->getNativeHandle();
            err_no = mysql_errno(conn_handle);
            sql_state = mysql_sqlstate(conn_handle);
            err_msg = mysql_error(conn_handle);
        } else {
            m_last_error = MySqlTransportError(MySqlTransportError::Category::InternalError, "Cannot get MySQL error: no valid statement or connection handle.", 0, nullptr, nullptr, 0, m_original_query);
            return;
        }

        if (err_no != 0) {
            m_last_error = MySqlTransportError(MySqlTransportError::Category::QueryError, (err_msg && err_msg[0] != '\0') ? std::string(err_msg) : "Unknown MySQL statement error", static_cast<int>(err_no), sql_state, err_msg, 0, m_original_query);
        } else {
            if (m_last_error.isOk()) {
                m_last_error = MySqlTransportError(MySqlTransportError::Category::InternalError, "MySQL API call failed without setting specific MySQL error code.", 0, nullptr, nullptr, 0, m_original_query);
            }
        }
    }

    void MySqlTransportStatement::setErrorFromProtocol(const mysql_protocol::MySqlProtocolError& proto_err, const std::string& context) {
        m_last_error = MySqlTransportError(MySqlTransportError::Category::ProtocolError, context + ": " + proto_err.error_message, 0, proto_err.sql_state, nullptr, proto_err.error_code, m_original_query);
    }

}  // namespace cpporm_mysql_transport