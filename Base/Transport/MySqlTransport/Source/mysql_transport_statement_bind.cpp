#include <mysql/mysql.h>

#include <cstring>  // For std::memset

#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "mysql_protocol/mysql_type_converter.h"

namespace cpporm_mysql_transport {

    bool MySqlTransportStatement::bindParam(unsigned int pos_zero_based, const MySqlTransportBindParam& param) {
        // 更正：首先检查是否是工具类命令
        if (m_is_utility_command) {
            setError(MySqlTransportError::Category::ApiUsageError, "Cannot bind parameters to a utility command (e.g., SHOW, DESCRIBE).");
            return false;
        }

        if (!m_stmt_handle) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle not initialized for bindParam.");
            return false;
        }
        if (!m_is_prepared) {
            if (!prepare()) {
                return false;
            }
        }
        if (pos_zero_based >= m_bind_buffers.size()) {
            setError(MySqlTransportError::Category::ApiUsageError, "Bind position out of range.");
            return false;
        }

        clearError();

        // 重置特定的绑定缓冲区条目
        std::memset(&m_bind_buffers[pos_zero_based], 0, sizeof(MYSQL_BIND));

        auto& bind_struct = m_bind_buffers[pos_zero_based];
        auto& data_buffer = m_param_data_buffers[pos_zero_based];
        auto& is_null_indicator = m_param_is_null_indicators[pos_zero_based];
        auto& length_indicator = m_param_length_indicators[pos_zero_based];

        const auto& native_value = param.value;

        if (native_value.is_null()) {
            auto result = mysql_protocol::setupMySqlBindForNull(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), MYSQL_TYPE_NULL);
            if (!result) {
                setErrorFromProtocol(result.error(), "Failed to setup bind for NULL");
                return false;
            }
            // 对于 setupMySqlBindForNull, buffer 和其他指针成员由函数设置
            return true;
        }

        std::expected<void, mysql_protocol::MySqlProtocolError> result;

        if (std::holds_alternative<bool>(native_value.data)) {
            // 对于基本类型，数据直接存在成员中，buffer指向该成员
            data_buffer.resize(sizeof(bool));
            *reinterpret_cast<bool*>(data_buffer.data()) = std::get<bool>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), std::get<bool>(native_value.data));
        } else if (std::holds_alternative<int8_t>(native_value.data)) {
            data_buffer.resize(sizeof(int8_t));
            *reinterpret_cast<int8_t*>(data_buffer.data()) = std::get<int8_t>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), native_value.original_mysql_flags & UNSIGNED_FLAG, *reinterpret_cast<int8_t*>(data_buffer.data()));
        } else if (std::holds_alternative<uint8_t>(native_value.data)) {
            data_buffer.resize(sizeof(uint8_t));
            *reinterpret_cast<uint8_t*>(data_buffer.data()) = std::get<uint8_t>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), true, *reinterpret_cast<int8_t*>(data_buffer.data()));
        } else if (std::holds_alternative<int16_t>(native_value.data)) {
            data_buffer.resize(sizeof(int16_t));
            *reinterpret_cast<int16_t*>(data_buffer.data()) = std::get<int16_t>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), native_value.original_mysql_flags & UNSIGNED_FLAG, *reinterpret_cast<int16_t*>(data_buffer.data()));
        } else if (std::holds_alternative<uint16_t>(native_value.data)) {
            data_buffer.resize(sizeof(uint16_t));
            *reinterpret_cast<uint16_t*>(data_buffer.data()) = std::get<uint16_t>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), true, *reinterpret_cast<int16_t*>(data_buffer.data()));
        } else if (std::holds_alternative<int32_t>(native_value.data)) {
            data_buffer.resize(sizeof(int32_t));
            *reinterpret_cast<int32_t*>(data_buffer.data()) = std::get<int32_t>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), native_value.original_mysql_flags & UNSIGNED_FLAG, *reinterpret_cast<int32_t*>(data_buffer.data()));
        } else if (std::holds_alternative<uint32_t>(native_value.data)) {
            data_buffer.resize(sizeof(uint32_t));
            *reinterpret_cast<uint32_t*>(data_buffer.data()) = std::get<uint32_t>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), true, *reinterpret_cast<int32_t*>(data_buffer.data()));
        } else if (std::holds_alternative<int64_t>(native_value.data)) {
            data_buffer.resize(sizeof(int64_t));
            *reinterpret_cast<int64_t*>(data_buffer.data()) = std::get<int64_t>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), native_value.original_mysql_flags & UNSIGNED_FLAG, *reinterpret_cast<int64_t*>(data_buffer.data()));
        } else if (std::holds_alternative<uint64_t>(native_value.data)) {
            data_buffer.resize(sizeof(uint64_t));
            *reinterpret_cast<uint64_t*>(data_buffer.data()) = std::get<uint64_t>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), true, *reinterpret_cast<int64_t*>(data_buffer.data()));
        } else if (std::holds_alternative<float>(native_value.data)) {
            data_buffer.resize(sizeof(float));
            *reinterpret_cast<float*>(data_buffer.data()) = std::get<float>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), *reinterpret_cast<float*>(data_buffer.data()));
        } else if (std::holds_alternative<double>(native_value.data)) {
            data_buffer.resize(sizeof(double));
            *reinterpret_cast<double*>(data_buffer.data()) = std::get<double>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInput(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), *reinterpret_cast<double*>(data_buffer.data()));
        } else if (std::holds_alternative<std::string>(native_value.data)) {
            const auto& str = std::get<std::string>(native_value.data);
            data_buffer.assign(str.begin(), str.end());
            result = mysql_protocol::setupMySqlBindForInputString(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), &length_indicator, native_value.original_mysql_type, reinterpret_cast<char*>(data_buffer.data()), data_buffer.size());
        } else if (std::holds_alternative<std::vector<unsigned char>>(native_value.data)) {
            data_buffer = std::get<std::vector<unsigned char>>(native_value.data);
            result = mysql_protocol::setupMySqlBindForInputBlob(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), &length_indicator, native_value.original_mysql_type, data_buffer.data(), data_buffer.size());
        } else if (std::holds_alternative<MYSQL_TIME>(native_value.data)) {
            data_buffer.resize(sizeof(MYSQL_TIME));
            std::memcpy(data_buffer.data(), &std::get<MYSQL_TIME>(native_value.data), sizeof(MYSQL_TIME));
            result = mysql_protocol::setupMySqlBindForInputTime(bind_struct, reinterpret_cast<bool*>(&is_null_indicator), native_value.original_mysql_type, reinterpret_cast<MYSQL_TIME*>(data_buffer.data()));
        } else {
            result = std::unexpected(mysql_protocol::MySqlProtocolError(mysql_protocol::InternalErrc::CONVERSION_UNSUPPORTED_TYPE, "Unsupported type for binding in MySqlTransportStatement"));
        }

        if (!result) {
            setErrorFromProtocol(result.error(), "Failed to setup bind for input parameter at pos " + std::to_string(pos_zero_based));
            return false;
        }

        // *** FIX START ***
        // 之前的问题：对于数字类型，setupMySqlBindForInput函数没有设置buffer指针。
        // 修复：在这里统一为所有非NULL且buffer指针尚未被设置的类型（主要是数字类型）设置buffer指针。
        if (!native_value.is_null() && bind_struct.buffer == nullptr) {
            bind_struct.buffer = data_buffer.data();
        }
        // *** FIX END ***

        return true;
    }

    bool MySqlTransportStatement::bindParams(const std::vector<MySqlTransportBindParam>& params) {
        // 更正：这是修复 SHOW/DESCRIBE 命令问题的关键
        if (m_is_utility_command) {
            if (!params.empty()) {
                setError(MySqlTransportError::Category::ApiUsageError, "Cannot bind parameters to a utility command (e.g., SHOW, DESCRIBE).");
                return false;
            }
            return true;  // 对工具类命令“绑定”零个参数是合法的
        }

        if (!m_stmt_handle) {
            setError(MySqlTransportError::Category::ApiUsageError, "Statement handle not initialized for bindParams.");
            return false;
        }

        if (!m_is_prepared) {
            if (!prepare()) {
                return false;
            }
        }

        if (params.size() != m_bind_buffers.size()) {
            setError(MySqlTransportError::Category::ApiUsageError, "Parameter count mismatch. Expected " + std::to_string(m_bind_buffers.size()) + ", got " + std::to_string(params.size()) + ".");
            return false;
        }

        for (size_t i = 0; i < params.size(); ++i) {
            if (!bindParam(static_cast<unsigned int>(i), params[i])) {
                // bindParam 已经设置了错误
                return false;
            }
        }

        if (mysql_stmt_bind_param(m_stmt_handle, m_bind_buffers.data()) != 0) {
            setErrorFromStatementHandle("mysql_stmt_bind_param failed");
            return false;
        }

        return true;
    }

}  // namespace cpporm_mysql_transport