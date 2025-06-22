// MySqlTransport/Source/mysql_transport_statement_bind.cpp
#include <mysql/mysql.h>

#include <cstring>  // For std::memset, std::memcpy
#include <variant>  // For std::visit
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "mysql_protocol/mysql_type_converter.h"

namespace cpporm_mysql_transport {

    bool MySqlTransportStatement::bindParam(unsigned int pos_zero_based, const MySqlTransportBindParam& param) {
        if (!m_stmt_handle) {
            // 如果 m_stmt_handle 为空，则错误应该与连接相关，或者是一个API使用错误
            // 之前setError会尝试从m_stmt_handle或m_connection获取错误，但这里m_stmt_handle明确为空
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle not initialized for bindParam.");
            return false;
        }
        if (!m_is_prepared) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement not prepared for bindParam.");
            return false;
        }
        unsigned long total_params = mysql_stmt_param_count(m_stmt_handle);
        if (pos_zero_based >= total_params) {
            setError(MySqlTransportError::Category::ApiUsageError, "Parameter position out of bounds.");
            return false;
        }

        if (m_bind_buffers.size() != total_params) {
            m_bind_buffers.assign(total_params, MYSQL_BIND{});
            m_param_data_buffers.assign(total_params, std::vector<unsigned char>());
            m_param_is_null_indicators.assign(total_params, static_cast<unsigned char>(0));
            m_param_length_indicators.assign(total_params, 0UL);
        }

        MYSQL_BIND& current_mysql_bind = m_bind_buffers[pos_zero_based];
        std::memset(static_cast<void*>(&current_mysql_bind), 0, sizeof(MYSQL_BIND));

        const mysql_protocol::MySqlNativeValue& native_val = param.value;

        m_param_is_null_indicators[pos_zero_based] = native_val.is_null() ? 1 : 0;
        current_mysql_bind.is_null = reinterpret_cast<bool*>(&m_param_is_null_indicators[pos_zero_based]);
        current_mysql_bind.buffer_type = native_val.original_mysql_type;
        current_mysql_bind.is_unsigned = (native_val.original_mysql_flags & UNSIGNED_FLAG) ? 1U : 0U;
        current_mysql_bind.length = &m_param_length_indicators[pos_zero_based];

        std::visit(
            [&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    current_mysql_bind.buffer = nullptr;
                    m_param_length_indicators[pos_zero_based] = 0;
                    current_mysql_bind.buffer_length = 0;
                } else if constexpr (std::is_same_v<T, bool>) {
                    m_param_data_buffers[pos_zero_based].resize(sizeof(unsigned char));
                    unsigned char bool_as_uchar = arg ? 1 : 0;
                    std::memcpy(m_param_data_buffers[pos_zero_based].data(), &bool_as_uchar, sizeof(unsigned char));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    current_mysql_bind.buffer_length = sizeof(unsigned char);
                    m_param_length_indicators[pos_zero_based] = sizeof(unsigned char);
                    current_mysql_bind.buffer_type = MYSQL_TYPE_TINY;
                    current_mysql_bind.is_unsigned = 0;
                } else if constexpr (std::is_same_v<T, int8_t>) {
                    m_param_data_buffers[pos_zero_based].resize(sizeof(T));
                    std::memcpy(m_param_data_buffers[pos_zero_based].data(), &arg, sizeof(T));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    current_mysql_bind.buffer_length = sizeof(T);
                    m_param_length_indicators[pos_zero_based] = sizeof(T);
                } else if constexpr (std::is_same_v<T, uint8_t>) {
                    m_param_data_buffers[pos_zero_based].resize(sizeof(T));
                    std::memcpy(m_param_data_buffers[pos_zero_based].data(), &arg, sizeof(T));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    current_mysql_bind.buffer_length = sizeof(T);
                    m_param_length_indicators[pos_zero_based] = sizeof(T);
                } else if constexpr (std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t>) {
                    m_param_data_buffers[pos_zero_based].resize(sizeof(T));
                    std::memcpy(m_param_data_buffers[pos_zero_based].data(), &arg, sizeof(T));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    current_mysql_bind.buffer_length = sizeof(T);
                    m_param_length_indicators[pos_zero_based] = sizeof(T);
                } else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) {
                    m_param_data_buffers[pos_zero_based].resize(sizeof(T));
                    std::memcpy(m_param_data_buffers[pos_zero_based].data(), &arg, sizeof(T));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    current_mysql_bind.buffer_length = sizeof(T);
                    m_param_length_indicators[pos_zero_based] = sizeof(T);
                } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
                    m_param_data_buffers[pos_zero_based].resize(sizeof(T));
                    std::memcpy(m_param_data_buffers[pos_zero_based].data(), &arg, sizeof(T));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    current_mysql_bind.buffer_length = sizeof(T);
                    m_param_length_indicators[pos_zero_based] = sizeof(T);
                } else if constexpr (std::is_same_v<T, float>) {
                    m_param_data_buffers[pos_zero_based].resize(sizeof(float));
                    std::memcpy(m_param_data_buffers[pos_zero_based].data(), &arg, sizeof(float));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    current_mysql_bind.buffer_length = sizeof(float);
                    m_param_length_indicators[pos_zero_based] = sizeof(float);
                } else if constexpr (std::is_same_v<T, double>) {
                    m_param_data_buffers[pos_zero_based].resize(sizeof(double));
                    std::memcpy(m_param_data_buffers[pos_zero_based].data(), &arg, sizeof(double));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    current_mysql_bind.buffer_length = sizeof(double);
                    m_param_length_indicators[pos_zero_based] = sizeof(double);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    m_param_data_buffers[pos_zero_based].assign(reinterpret_cast<const unsigned char*>(arg.data()), reinterpret_cast<const unsigned char*>(arg.data() + arg.length()));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    m_param_length_indicators[pos_zero_based] = static_cast<unsigned long>(arg.length());
                    current_mysql_bind.buffer_length = m_param_length_indicators[pos_zero_based];
                } else if constexpr (std::is_same_v<T, std::vector<unsigned char>>) {
                    m_param_data_buffers[pos_zero_based] = arg;
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    m_param_length_indicators[pos_zero_based] = static_cast<unsigned long>(m_param_data_buffers[pos_zero_based].size());
                    current_mysql_bind.buffer_length = m_param_length_indicators[pos_zero_based];
                } else if constexpr (std::is_same_v<T, MYSQL_TIME>) {
                    m_param_data_buffers[pos_zero_based].resize(sizeof(MYSQL_TIME));
                    std::memcpy(m_param_data_buffers[pos_zero_based].data(), &arg, sizeof(MYSQL_TIME));
                    current_mysql_bind.buffer = static_cast<void*>(m_param_data_buffers[pos_zero_based].data());
                    current_mysql_bind.buffer_length = sizeof(MYSQL_TIME);
                    m_param_length_indicators[pos_zero_based] = 0;  // Length is not used for MYSQL_TIME input if buffer_length is set
                } else {
                    setError(MySqlTransportError::Category::ApiUsageError, "Unsupported type in MySqlNativeValue for binding parameter at pos " + std::to_string(pos_zero_based));
                    m_param_is_null_indicators[pos_zero_based] = 1;
                    current_mysql_bind.buffer_type = MYSQL_TYPE_NULL;
                    current_mysql_bind.buffer = nullptr;
                    m_param_length_indicators[pos_zero_based] = 0;
                    current_mysql_bind.buffer_length = 0;
                }
            },
            native_val.data);

        return true;
    }

    bool MySqlTransportStatement::bindParams(const std::vector<MySqlTransportBindParam>& params) {
        if (!m_stmt_handle) {
            // setErrorFromMySQL(); // 无参数版本，会尝试从 m_stmt_handle 或 m_connection 获取
            // 此时 m_stmt_handle 为 nullptr，setErrorFromMySQL 会尝试 m_connection
            // 或者更直接地设置错误：
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle not initialized for bindParams.");
            return false;
        }
        if (!m_is_prepared) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement not prepared for bindParams.");
            return false;
        }

        unsigned long expected_param_count = mysql_stmt_param_count(m_stmt_handle);
        if (params.size() != expected_param_count) {
            setError(MySqlTransportError::Category::ApiUsageError, "Incorrect number of parameters supplied for bindParams. Expected " + std::to_string(expected_param_count) + ", got " + std::to_string(params.size()));
            return false;
        }

        if (expected_param_count == 0) {
            return true;
        }

        m_bind_buffers.assign(expected_param_count, MYSQL_BIND{});
        m_param_data_buffers.assign(expected_param_count, std::vector<unsigned char>());
        m_param_is_null_indicators.assign(expected_param_count, static_cast<unsigned char>(0));
        m_param_length_indicators.assign(expected_param_count, 0UL);

        for (unsigned int i = 0; i < params.size(); ++i) {
            if (!bindParam(i, params[i])) {  // bindParam 内部会设置错误
                return false;
            }
        }

        if (mysql_stmt_bind_param(m_stmt_handle, m_bind_buffers.data()) != 0) {
            // 在 mysql_stmt_bind_param 失败时，错误信息应该从语句句柄 m_stmt_handle 获取
            setErrorFromMySQL(reinterpret_cast<MYSQL*>(m_stmt_handle), "mysql_stmt_bind_param failed");
            return false;
        }
        return true;
    }

}  // namespace cpporm_mysql_transport