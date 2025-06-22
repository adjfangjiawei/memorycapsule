// cpporm_mysql_transport/mysql_transport_result_prepared_bind.cpp
#include <mysql/mysql.h>

#include <cstring>  // For std::memset

#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"  // For m_statement (used for getError)

namespace cpporm_mysql_transport {

    void MySqlTransportResult::setupOutputBindBuffers() {
        if (!m_is_from_prepared_statement || m_field_count == 0 || !m_mysql_stmt_handle_for_fetch) return;
        if (m_fields_meta.size() != m_field_count) {
            m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::InternalError, "Field metadata count mismatch in setupOutputBindBuffers.");
            m_is_valid = false;
            return;
        }

        m_output_bind_buffers.assign(m_field_count, MYSQL_BIND{});
        m_output_data_buffers.resize(m_field_count);

        m_output_is_null_indicators.assign(m_field_count, (char)0);
        m_output_length_indicators.assign(m_field_count, 0UL);
        m_output_error_indicators.assign(m_field_count, (char)0);

        for (unsigned int i = 0; i < m_field_count; ++i) {
            MYSQL_BIND& bind = m_output_bind_buffers[i];
            const MySqlTransportFieldMeta& meta = m_fields_meta[i];

            std::memset(&bind, 0, sizeof(MYSQL_BIND));

            bind.buffer_type = meta.native_type_id;
            unsigned long buffer_sz = 0;
            buffer_sz = meta.length;

            switch (meta.native_type_id) {
                case MYSQL_TYPE_TINY:
                    if (buffer_sz < sizeof(int8_t)) buffer_sz = sizeof(int8_t);
                    break;
                case MYSQL_TYPE_SHORT:
                    if (buffer_sz < sizeof(int16_t)) buffer_sz = sizeof(int16_t);
                    break;
                case MYSQL_TYPE_INT24:
                case MYSQL_TYPE_LONG:
                    if (buffer_sz < sizeof(int32_t)) buffer_sz = sizeof(int32_t);
                    break;
                case MYSQL_TYPE_LONGLONG:
                    if (buffer_sz < sizeof(int64_t)) buffer_sz = sizeof(int64_t);
                    break;
                case MYSQL_TYPE_FLOAT:
                    if (buffer_sz < sizeof(float)) buffer_sz = sizeof(float);
                    break;
                case MYSQL_TYPE_DOUBLE:
                    if (buffer_sz < sizeof(double)) buffer_sz = sizeof(double);
                    break;
                case MYSQL_TYPE_BIT:
                    buffer_sz = (meta.length + 7) / 8;
                    if (buffer_sz == 0) buffer_sz = 1;
                    break;
                case MYSQL_TYPE_DATE:
                case MYSQL_TYPE_TIME:
                case MYSQL_TYPE_DATETIME:
                case MYSQL_TYPE_TIMESTAMP:
                case MYSQL_TYPE_YEAR:
                    buffer_sz = sizeof(MYSQL_TIME);
                    break;
                case MYSQL_TYPE_DECIMAL:
                case MYSQL_TYPE_NEWDECIMAL:
                    if (buffer_sz == 0)
                        buffer_sz = 66;
                    else if (buffer_sz < 66)
                        buffer_sz = 66;
                    break;
                case MYSQL_TYPE_STRING:
                case MYSQL_TYPE_VAR_STRING:
                case MYSQL_TYPE_VARCHAR:
                case MYSQL_TYPE_BLOB:
                case MYSQL_TYPE_TINY_BLOB:
                case MYSQL_TYPE_MEDIUM_BLOB:
                case MYSQL_TYPE_LONG_BLOB:
                case MYSQL_TYPE_JSON:
                case MYSQL_TYPE_ENUM:
                case MYSQL_TYPE_SET:
                case MYSQL_TYPE_GEOMETRY:
                    if (buffer_sz == 0)
                        buffer_sz = (meta.max_length > 0) ? meta.max_length : 256;
                    else if (meta.max_length > 0 && buffer_sz < meta.max_length)
                        buffer_sz = meta.max_length;
                    if (buffer_sz == 0) buffer_sz = 256;
                    break;
                default:
                    if (buffer_sz == 0) buffer_sz = 256;
                    break;
            }
            if (buffer_sz == 0) buffer_sz = 1;

            m_output_data_buffers[i].assign(buffer_sz, (unsigned char)0);
            bind.buffer = m_output_data_buffers[i].data();
            bind.buffer_length = buffer_sz;
            bind.length = &m_output_length_indicators[i];
            bind.is_null = reinterpret_cast<bool*>(&m_output_is_null_indicators[i]);
            bind.error = reinterpret_cast<bool*>(&m_output_error_indicators[i]);
            bind.is_unsigned = (meta.flags & UNSIGNED_FLAG);
        }

        if (mysql_stmt_bind_result(m_mysql_stmt_handle_for_fetch, m_output_bind_buffers.data()) != 0) {
            if (m_statement)
                m_error_collector_owned = m_statement->getError();
            else if (m_mysql_stmt_handle_for_fetch)
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_bind_result failed.", mysql_stmt_errno(m_mysql_stmt_handle_for_fetch), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
            else
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_bind_result failed (no statement context).");
            m_is_valid = false;
        }
    }

}  // namespace cpporm_mysql_transport