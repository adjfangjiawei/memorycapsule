// cpporm_mysql_transport/mysql_transport_statement.cpp
#include "cpporm_mysql_transport/mysql_transport_statement.h"

#include <mysql/mysql.h>
// #include <mysql/errmsg.h> // CR_NO_MORE_RESULTS is usually here, but we'll use -1 check

#include <algorithm>
#include <cstring>
#include <variant>  // For std::visit

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "mysql_protocol/mysql_type_converter.h"

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

    bool MySqlTransportStatement::prepare() {
        if (!m_stmt_handle) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle is not initialized for prepare.");
            return false;
        }
        if (m_is_prepared) {
            return true;
        }
        clearError();

        if (mysql_stmt_prepare(m_stmt_handle, m_original_query.c_str(), m_original_query.length()) != 0) {
            setErrorFromMySQL();
            m_is_prepared = false;
            return false;
        }

        m_is_prepared = true;
        unsigned long param_count_long = mysql_stmt_param_count(m_stmt_handle);
        if (param_count_long > 0) {
            unsigned int param_count = static_cast<unsigned int>(param_count_long);
            m_bind_buffers.assign(param_count, MYSQL_BIND{});
            m_param_data_buffers.resize(param_count);
            m_param_is_null_indicators.assign(param_count, (char)0);
            m_param_length_indicators.assign(param_count, 0UL);
        } else {
            m_bind_buffers.clear();
            m_param_data_buffers.clear();
            m_param_is_null_indicators.clear();
            m_param_length_indicators.clear();
        }
        return true;
    }

    bool MySqlTransportStatement::isPrepared() const {
        return m_is_prepared;
    }

    bool MySqlTransportStatement::bindParam(unsigned int pos, const MySqlTransportBindParam& param_wrapper) {
        if (!m_stmt_handle || !m_is_prepared) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement not prepared for bindParam.");
            return false;
        }
        unsigned long expected_param_count_long = mysql_stmt_param_count(m_stmt_handle);
        if (pos >= expected_param_count_long) {
            setError(MySqlTransportError::Category::ApiUsageError, "Parameter position out of bounds for bindParam.");
            return false;
        }
        clearError();

        MYSQL_BIND& current_bind = m_bind_buffers[pos];  // Removed `¤` typo
        const mysql_protocol::MySqlNativeValue& native_value = param_wrapper.value;

        std::memset(&current_bind, 0, sizeof(MYSQL_BIND));

        if (native_value.is_null()) {
            m_param_is_null_indicators[pos] = 1;
            current_bind.is_null = reinterpret_cast<bool*>(&m_param_is_null_indicators[pos]);
            current_bind.buffer_type = MYSQL_TYPE_NULL;
        } else {
            m_param_is_null_indicators[pos] = 0;
            current_bind.is_null = reinterpret_cast<bool*>(&m_param_is_null_indicators[pos]);
            current_bind.buffer_type = native_value.original_mysql_type;
            current_bind.is_unsigned = (native_value.original_mysql_flags & UNSIGNED_FLAG) != 0;

            bool processed_by_visit = false;
            std::visit(
                [&](const auto& val_variant) {
                    using T = std::decay_t<decltype(val_variant)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        m_param_is_null_indicators[pos] = 1;
                        current_bind.is_null = reinterpret_cast<bool*>(&m_param_is_null_indicators[pos]);
                        current_bind.buffer_type = MYSQL_TYPE_NULL;
                        processed_by_visit = true;
                    } else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, int64_t> ||
                                         std::is_same_v<T, uint64_t> || std::is_same_v<T, float> || std::is_same_v<T, double>) {
                        m_param_data_buffers[pos].resize(sizeof(T));
                        std::memcpy(m_param_data_buffers[pos].data(), &val_variant, sizeof(T));
                        current_bind.buffer = m_param_data_buffers[pos].data();
                        current_bind.buffer_length = sizeof(T);
                        current_bind.length = nullptr;
                        processed_by_visit = true;
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        m_param_data_buffers[pos].assign(val_variant.begin(), val_variant.end());
                        current_bind.buffer = m_param_data_buffers[pos].data();
                        m_param_length_indicators[pos] = static_cast<unsigned long>(val_variant.length());  // Corrected typo `leng¤`
                        current_bind.length = &m_param_length_indicators[pos];
                        current_bind.buffer_length = static_cast<unsigned long>(val_variant.length());
                        processed_by_visit = true;
                    } else if constexpr (std::is_same_v<T, std::vector<unsigned char>>) {
                        m_param_data_buffers[pos].assign(val_variant.begin(), val_variant.end());
                        current_bind.buffer = m_param_data_buffers[pos].data();
                        m_param_length_indicators[pos] = static_cast<unsigned long>(val_variant.size());
                        current_bind.length = &m_param_length_indicators[pos];
                        current_bind.buffer_length = static_cast<unsigned long>(val_variant.size());
                        processed_by_visit = true;
                    } else if constexpr (std::is_same_v<T, MYSQL_TIME>) {
                        m_param_data_buffers[pos].resize(sizeof(MYSQL_TIME));
                        std::memcpy(m_param_data_buffers[pos].data(), &val_variant, sizeof(MYSQL_TIME));
                        current_bind.buffer = m_param_data_buffers[pos].data();
                        current_bind.buffer_length = sizeof(MYSQL_TIME);
                        current_bind.length = nullptr;
                        processed_by_visit = true;
                    }
                },
                native_value.data);

            if (!processed_by_visit) {
                mysql_protocol::MySqlProtocolError proto_err(mysql_protocol::InternalErrc::CONVERSION_UNSUPPORTED_TYPE, "Unsupported type in MySqlNativeValue for binding at pos " + std::to_string(pos));
                setErrorFromProtocol(proto_err, "Failed to setup MYSQL_BIND for parameter " + std::to_string(pos));
                return false;
            }
        }
        return true;
    }

    bool MySqlTransportStatement::bindParams(const std::vector<MySqlTransportBindParam>& params) {
        if (!m_stmt_handle || !m_is_prepared) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement not prepared for bindParams.");
            return false;
        }
        unsigned long expected_param_count_long = mysql_stmt_param_count(m_stmt_handle);
        if (params.size() != static_cast<size_t>(expected_param_count_long)) {
            setError(MySqlTransportError::Category::ApiUsageError, "Incorrect number of parameters supplied. Expected " + std::to_string(expected_param_count_long) + ", got " + std::to_string(params.size()));
            return false;
        }
        if (params.empty() && expected_param_count_long == 0) {
            return true;
        }
        if (m_bind_buffers.size() != expected_param_count_long) {
            setError(MySqlTransportError::Category::InternalError, "Internal bind buffer size mismatch.");
            return false;
        }

        for (unsigned int i = 0; i < params.size(); ++i) {
            if (!bindParam(i, params[i])) {
                return false;
            }
        }

        if (expected_param_count_long > 0) {
            if (mysql_stmt_bind_param(m_stmt_handle, m_bind_buffers.data()) != 0) {
                setErrorFromMySQL();
                return false;
            }
        }
        return true;
    }

    std::optional<my_ulonglong> MySqlTransportStatement::execute() {
        if (!m_stmt_handle) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle not initialized for execute.");
            return std::nullopt;
        }
        if (!m_is_prepared) {
            if (!prepare()) {
                return std::nullopt;
            }
        }
        clearError();
        m_affected_rows = 0;
        m_last_insert_id = 0;
        m_warning_count = 0;

        if (mysql_stmt_execute(m_stmt_handle) != 0) {
            setErrorFromMySQL();
            return std::nullopt;
        }

        m_affected_rows = mysql_stmt_affected_rows(m_stmt_handle);
        m_last_insert_id = mysql_stmt_insert_id(m_stmt_handle);
        if (m_connection && m_connection->getNativeHandle()) {
            m_warning_count = mysql_warning_count(m_connection->getNativeHandle());
        }

        int status;
        do {
            MYSQL_RES* meta = mysql_stmt_result_metadata(m_stmt_handle);
            if (meta) {
                mysql_free_result(meta);
            }
            // If mysql_stmt_store_result was called by a previous executeQuery AND its MySqlTransportResult
            // object is still alive and holding the stored result, calling mysql_stmt_next_result
            // might be problematic or unnecessary until that result is freed.
            // However, if the MySqlTransportResult object was destroyed, it should have freed the stored result.
            // For a simple execute() not intended to return rows to the client directly through this path,
            // just consuming next_result is usually to clear the pipe for subsequent operations.
            // If a result set was stored (e.g. from executeQuery), mysql_stmt_free_result(m_stmt_handle)
            // should have been called by MySqlTransportResult's destructor.
            // If it's a new execution, any prior results on the statement handle should be implicitly
            // cleared or this call will advance past them.
            status = mysql_stmt_next_result(m_stmt_handle);
            if (status > 0) {
                setErrorFromMySQL();
                return std::nullopt;
            }
        } while (status == 0);

        // After loop, status is -1 (no more results) or error (status > 0, handled).
        // mysql_stmt_errno might still hold an error if mysql_stmt_next_result itself had issues
        // that weren't just "no more results".
        // The CR_NO_MORE_RESULTS is not for mysql_stmt_errno directly with mysql_stmt_next_result.
        // mysql_stmt_next_result returns -1 for no more results.
        if (status > 0) {         // This condition should already be caught inside the loop
            setErrorFromMySQL();  // Error set if status > 0
            return std::nullopt;
        }

        return m_affected_rows;
    }

    std::unique_ptr<MySqlTransportResult> MySqlTransportStatement::executeQuery() {
        if (!m_stmt_handle) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle not initialized for executeQuery.");
            return nullptr;
        }
        if (!m_is_prepared) {
            if (!prepare()) {
                return nullptr;
            }
        }
        clearError();
        m_affected_rows = 0;
        m_last_insert_id = 0;
        m_warning_count = 0;

        // If this statement was used for a previous query and its result was stored
        // by mysql_stmt_store_result (e.g. by a MySqlTransportResult object),
        // that result must be freed before re-executing or getting new metadata.
        // MySqlTransportResult's destructor calls mysql_stmt_free_result.
        // If the MySqlTransportResult object went out of scope, this should be fine.
        // If we want to be absolutely safe and allow reuse of the statement object
        // for multiple executeQuery calls without explicit reset, we might call
        // mysql_stmt_free_result(m_stmt_handle); here.
        // However, the typical pattern is one MySqlTransportStatement per logical query execution.

        if (mysql_stmt_execute(m_stmt_handle) != 0) {
            setErrorFromMySQL();
            return nullptr;
        }

        MYSQL_RES* meta_res_handle = mysql_stmt_result_metadata(m_stmt_handle);

        if (!meta_res_handle) {
            if (mysql_stmt_errno(m_stmt_handle) != 0) {
                setErrorFromMySQL();
                return nullptr;  // Error occurred
            } else if (mysql_stmt_field_count(m_stmt_handle) == 0) {
                // This is a valid scenario (e.g. "SELECT ... WHERE 1=0" or DML result)
                // MySqlTransportResult constructor will handle meta_res_handle being NULL
                // if field_count is also 0.
            } else {
                // field_count > 0 but metadata is NULL, this is an unexpected error.
                setError(MySqlTransportError::Category::QueryError, "Failed to get result metadata after executeQuery, but fields were expected.");
                return nullptr;
            }
        }

        if (m_connection && m_connection->getNativeHandle()) {
            m_warning_count = mysql_warning_count(m_connection->getNativeHandle());
        }

        return std::make_unique<MySqlTransportResult>(this, meta_res_handle, m_last_error);
    }

    my_ulonglong MySqlTransportStatement::getAffectedRows() const {
        return m_affected_rows;
    }

    my_ulonglong MySqlTransportStatement::getLastInsertId() const {
        return m_last_insert_id;
    }

    unsigned int MySqlTransportStatement::getWarningCount() const {
        return m_warning_count;
    }

    MySqlTransportError MySqlTransportStatement::getError() const {
        return m_last_error;
    }

    void MySqlTransportStatement::close() {
        if (m_stmt_handle) {
            // MySqlTransportResult's destructor calls mysql_stmt_free_result if it stored results.
            // mysql_stmt_close itself also frees any un-freed results.
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
                m_last_error = MySqlTransportError(MySqlTransportError::Category::InternalError, "MySQL API call failed without setting specific error code.", 0, nullptr, nullptr, 0, m_original_query);
            }
        }
    }

    void MySqlTransportStatement::setErrorFromProtocol(const mysql_protocol::MySqlProtocolError& proto_err, const std::string& context) {
        m_last_error = MySqlTransportError(MySqlTransportError::Category::ProtocolError, context + ": " + proto_err.error_message, 0, proto_err.sql_state, nullptr, proto_err.error_code, m_original_query);
    }

}  // namespace cpporm_mysql_transport