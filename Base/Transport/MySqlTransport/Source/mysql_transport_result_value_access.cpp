// cpporm_mysql_transport/mysql_transport_result_value_access.cpp
#include <mysql/mysql.h>  // For MYSQL_FIELD

#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "mysql_protocol/mysql_type_converter.h"  // For MySqlNativeValue and conversion functions

namespace cpporm_mysql_transport {

    std::optional<mysql_protocol::MySqlNativeValue> MySqlTransportResult::getValue(unsigned int col_idx) {
        if (!m_is_valid || col_idx >= m_field_count) {
            m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "Invalid column index for getValue.");
            return std::nullopt;
        }
        if (m_fields_meta.size() <= col_idx) {
            m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::InternalError, "Field metadata inconsistent with field count in getValue.");
            return std::nullopt;
        }
        if (m_current_row_idx == -1) {  // No valid row fetched (either before first fetch, after last, or after an error)
            m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "No current valid row to get value from.");
            return std::nullopt;
        }

        if (m_is_from_prepared_statement) {
            if (!m_mysql_stmt_handle_for_fetch) {  // m_fetched_all_from_stmt implies no current row too
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "Invalid state for prepared statement getValue (no handle or past end).");
                return std::nullopt;
            }
            if (m_output_is_null_indicators.size() <= col_idx || m_output_bind_buffers.size() <= col_idx) {
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::InternalError, "Output bind/indicator buffers out of sync for prepared statement getValue.");
                return std::nullopt;
            }

            if (m_output_is_null_indicators[col_idx] != 0) {
                mysql_protocol::MySqlNativeValue nv;
                nv.original_mysql_type = m_fields_meta[col_idx].native_type_id;
                nv.original_mysql_flags = m_fields_meta[col_idx].flags;
                nv.original_charsetnr = m_fields_meta[col_idx].charsetnr;
                return nv;
            }

            auto expected_nv = mysql_protocol::mySqlBoundResultToNativeValue(&m_output_bind_buffers[col_idx], m_fields_meta[col_idx].flags, m_fields_meta[col_idx].charsetnr);
            if (expected_nv) {
                return expected_nv.value();
            } else {
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::ProtocolError, "Failed to convert bound result to NativeValue: " + expected_nv.error().error_message, 0, nullptr, nullptr, expected_nv.error().error_code);
                return std::nullopt;
            }

        } else {  // From non-prepared (MYSQL_RES)
            if (!m_current_sql_row) {
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "No current row fetched for non-prepared statement getValue.");
                return std::nullopt;
            }
            if (m_current_sql_row[col_idx] == nullptr) {
                mysql_protocol::MySqlNativeValue nv;
                nv.original_mysql_type = m_fields_meta[col_idx].native_type_id;
                nv.original_mysql_flags = m_fields_meta[col_idx].flags;
                nv.original_charsetnr = m_fields_meta[col_idx].charsetnr;
                return nv;
            }

            MYSQL_FIELD* field_info = mysql_fetch_field_direct(m_mysql_res_metadata, col_idx);
            if (!field_info) {
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::InternalError, "Failed to fetch field info for getValue.");
                return std::nullopt;
            }
            if (!m_current_lengths) {
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::InternalError, "Row lengths not available for non-prepared getValue.");
                return std::nullopt;
            }

            auto expected_nv = mysql_protocol::mySqlRowFieldToNativeValue(m_current_sql_row[col_idx], m_current_lengths[col_idx], field_info);
            if (expected_nv) {
                return expected_nv.value();
            } else {
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::ProtocolError, "Failed to convert row field to NativeValue: " + expected_nv.error().error_message, 0, nullptr, nullptr, expected_nv.error().error_code);
                return std::nullopt;
            }
        }
    }

    std::optional<mysql_protocol::MySqlNativeValue> MySqlTransportResult::getValue(const std::string& col_name) {
        int idx = getFieldIndex(col_name);
        if (idx == -1) {
            m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "Invalid column name for getValue: " + col_name);
            return std::nullopt;
        }
        return getValue(static_cast<unsigned int>(idx));
    }

    bool MySqlTransportResult::isNull(unsigned int col_idx) {
        if (!m_is_valid || col_idx >= m_field_count || m_current_row_idx == -1) {
            return true;
        }

        if (m_is_from_prepared_statement) {
            if (!m_mysql_stmt_handle_for_fetch) return true;
            if (m_output_is_null_indicators.size() <= col_idx) return true;
            return m_output_is_null_indicators[col_idx] != 0;
        } else {
            if (!m_current_sql_row) return true;
            return m_current_sql_row[col_idx] == nullptr;
        }
    }

    bool MySqlTransportResult::isNull(const std::string& col_name) {
        int idx = getFieldIndex(col_name);
        if (idx == -1) return true;
        return isNull(static_cast<unsigned int>(idx));
    }

    std::vector<mysql_protocol::MySqlNativeValue> MySqlTransportResult::getCurrentRowValues() {
        std::vector<mysql_protocol::MySqlNativeValue> row_values;
        if (!m_is_valid || m_field_count == 0 || m_current_row_idx == -1) {
            return row_values;
        }
        row_values.reserve(m_field_count);
        for (unsigned int i = 0; i < m_field_count; ++i) {
            auto val_opt = getValue(i);
            if (val_opt) {
                row_values.push_back(std::move(*val_opt));
            } else {
                // Error already set by getValue, push a default NULL value
                mysql_protocol::MySqlNativeValue null_val;
                if (m_fields_meta.size() > i) {
                    null_val.original_mysql_type = m_fields_meta[i].native_type_id;
                    null_val.original_mysql_flags = m_fields_meta[i].flags;
                    null_val.original_charsetnr = m_fields_meta[i].charsetnr;
                } else {
                    null_val.original_mysql_type = MYSQL_TYPE_NULL;
                }
                row_values.push_back(std::move(null_val));
            }
        }
        return row_values;
    }

}  // namespace cpporm_mysql_transport