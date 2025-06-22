// cpporm_mysql_transport/mysql_transport_result.cpp
#include "cpporm_mysql_transport/mysql_transport_result.h"

#include <mysql/mysql.h>

#include <cstring>  // For memset for MYSQL_BIND

#include "cpporm_mysql_transport/mysql_transport_statement.h"  // Needs statement handle
#include "mysql_protocol/mysql_type_converter.h"               // For mySqlRowFieldToNativeValue, mySqlBoundResultToNativeValue

namespace cpporm_mysql_transport {

    // Constructor for results from PREPARED STATEMENTS (MYSQL_STMT*)
    // The MYSQL_RES* passed here is METADATA ONLY.
    MySqlTransportResult::MySqlTransportResult(MySqlTransportStatement* stmt, MYSQL_RES* meta_res_handle, MySqlTransportError& err_ref)
        : m_statement(stmt),
          m_mysql_res_metadata(meta_res_handle),  // This is metadata from mysql_stmt_result_metadata()
          m_mysql_stmt_handle_for_fetch(stmt ? stmt->getNativeStatementHandle() : nullptr),
          m_error_collector(err_ref),  // Use the error collector from the statement
          m_current_sql_row(nullptr),  // Not used for prepared statements
          m_current_lengths(nullptr),  // Not used for prepared statements
          m_meta_populated(false),
          m_is_valid(false),
          m_is_from_prepared_statement(true),
          m_stmt_result_was_stored(false),  // Initialize new flag
          m_fetched_all_from_stmt(false) {
        if (!m_statement || !m_mysql_stmt_handle_for_fetch || !m_mysql_res_metadata) {
            m_error_collector = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "Invalid arguments to MySqlTransportResult constructor (prepared statement path).");
            if (m_mysql_res_metadata) {
                mysql_free_result(m_mysql_res_metadata);
            }
            m_mysql_res_metadata = nullptr;
            return;
        }

        if (mysql_stmt_store_result(m_mysql_stmt_handle_for_fetch) != 0) {
            m_error_collector = m_statement->getError();
            mysql_free_result(m_mysql_res_metadata);
            m_mysql_res_metadata = nullptr;
            // m_mysql_stmt_handle_for_fetch is not owned by result, statement owns it.
            m_stmt_result_was_stored = false;
            return;
        }
        m_stmt_result_was_stored = true;

        m_row_count = mysql_stmt_num_rows(m_mysql_stmt_handle_for_fetch);
        m_field_count = mysql_num_fields(m_mysql_res_metadata);

        if (m_field_count > 0) {
            populateFieldsMeta();
            if (!m_fields_meta.empty() && m_is_valid) {  // m_is_valid check added from populateFieldsMeta
                setupOutputBindBuffers();
            } else if (m_is_valid) {  // Meta empty but field_count > 0 and still valid (e.g. populate failed gracefully)
                mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                if (m_stmt_result_was_stored && m_mysql_stmt_handle_for_fetch) {
                    mysql_stmt_free_result(m_mysql_stmt_handle_for_fetch);
                }
                m_stmt_result_was_stored = false;
                m_is_valid = false;  // Mark as invalid now
                return;
            } else {  // Not valid after populateFieldsMeta
                // Error should be in m_error_collector
                if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                if (m_stmt_result_was_stored && m_mysql_stmt_handle_for_fetch) {
                    mysql_stmt_free_result(m_mysql_stmt_handle_for_fetch);
                }
                m_stmt_result_was_stored = false;
                return;  // m_is_valid is already false
            }
        }
        // If m_field_count is 0, it can still be a valid result (e.g., from an UPDATE statement result processing)
        // Only set m_is_valid to true if all previous steps that could invalidate it passed,
        // or if it's a 0-field result which is inherently valid after successful store_result.
        if (m_field_count == 0 || (m_field_count > 0 && m_is_valid)) {
            m_is_valid = true;
        }
    }

    // Constructor for results from NON-PREPARED STATEMENTS (MYSQL_RES* from mysql_store_result)
    MySqlTransportResult::MySqlTransportResult(MYSQL_RES* stored_res_handle, MySqlTransportError& err_ref)
        : m_statement(nullptr),
          m_mysql_res_metadata(stored_res_handle),
          m_mysql_stmt_handle_for_fetch(nullptr),
          m_error_collector(err_ref),
          m_current_sql_row(nullptr),
          m_current_lengths(nullptr),
          m_meta_populated(false),
          m_is_valid(false),
          m_is_from_prepared_statement(false),
          m_stmt_result_was_stored(false),
          m_fetched_all_from_stmt(false) {
        if (!m_mysql_res_metadata) {
            m_error_collector = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "Null MYSQL_RES handle passed to MySqlTransportResult constructor.");
            return;
        }
        m_row_count = mysql_num_rows(m_mysql_res_metadata);
        m_field_count = mysql_num_fields(m_mysql_res_metadata);

        if (m_field_count > 0) {
            populateFieldsMeta();
            if (!m_is_valid) {  // populateFieldsMeta might set m_is_valid to false
                if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                return;
            }
            if (m_fields_meta.empty() && m_field_count > 0) {
                if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                m_is_valid = false;
                return;
            }
        }
        m_is_valid = true;
    }

    MySqlTransportResult::~MySqlTransportResult() {
        clearCurrentRow();
        if (m_is_from_prepared_statement && m_mysql_stmt_handle_for_fetch && m_stmt_result_was_stored) {
            mysql_stmt_free_result(m_mysql_stmt_handle_for_fetch);
            m_stmt_result_was_stored = false;
        }
        if (m_mysql_res_metadata) {
            mysql_free_result(m_mysql_res_metadata);
            m_mysql_res_metadata = nullptr;
        }
    }

    // Move constructor
    MySqlTransportResult::MySqlTransportResult(MySqlTransportResult&& other) noexcept
        : m_statement(other.m_statement),
          m_mysql_res_metadata(other.m_mysql_res_metadata),
          m_mysql_stmt_handle_for_fetch(other.m_mysql_stmt_handle_for_fetch),
          m_error_collector(other.m_error_collector),
          m_fields_meta(std::move(other.m_fields_meta)),
          m_current_sql_row(other.m_current_sql_row),
          m_current_lengths(other.m_current_lengths),
          m_row_count(other.m_row_count),
          m_field_count(other.m_field_count),
          m_current_row_idx(other.m_current_row_idx),
          m_meta_populated(other.m_meta_populated),
          m_is_valid(other.m_is_valid),
          m_is_from_prepared_statement(other.m_is_from_prepared_statement),
          m_stmt_result_was_stored(other.m_stmt_result_was_stored),
          m_output_bind_buffers(std::move(other.m_output_bind_buffers)),
          m_output_data_buffers(std::move(other.m_output_data_buffers)),
          m_output_is_null_indicators(std::move(other.m_output_is_null_indicators)),
          m_output_length_indicators(std::move(other.m_output_length_indicators)),
          m_output_error_indicators(std::move(other.m_output_error_indicators)),
          m_fetched_all_from_stmt(other.m_fetched_all_from_stmt) {
        other.m_mysql_res_metadata = nullptr;
        other.m_mysql_stmt_handle_for_fetch = nullptr;
        other.m_current_sql_row = nullptr;
        other.m_is_valid = false;
        other.m_stmt_result_was_stored = false;
    }

    // Move assignment
    MySqlTransportResult& MySqlTransportResult::operator=(MySqlTransportResult&& other) noexcept {
        if (this != &other) {
            clearCurrentRow();
            if (m_is_from_prepared_statement && m_mysql_stmt_handle_for_fetch && m_stmt_result_was_stored) {
                mysql_stmt_free_result(m_mysql_stmt_handle_for_fetch);
            }
            if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);

            m_statement = other.m_statement;
            m_mysql_res_metadata = other.m_mysql_res_metadata;
            m_mysql_stmt_handle_for_fetch = other.m_mysql_stmt_handle_for_fetch;
            m_error_collector = other.m_error_collector;
            m_fields_meta = std::move(other.m_fields_meta);
            m_current_sql_row = other.m_current_sql_row;
            m_current_lengths = other.m_current_lengths;
            m_row_count = other.m_row_count;
            m_field_count = other.m_field_count;
            m_current_row_idx = other.m_current_row_idx;
            m_meta_populated = other.m_meta_populated;
            m_is_valid = other.m_is_valid;
            m_is_from_prepared_statement = other.m_is_from_prepared_statement;
            m_stmt_result_was_stored = other.m_stmt_result_was_stored;
            m_output_bind_buffers = std::move(other.m_output_bind_buffers);
            m_output_data_buffers = std::move(other.m_output_data_buffers);
            m_output_is_null_indicators = std::move(other.m_output_is_null_indicators);
            m_output_length_indicators = std::move(other.m_output_length_indicators);
            m_output_error_indicators = std::move(other.m_output_error_indicators);
            m_fetched_all_from_stmt = other.m_fetched_all_from_stmt;

            other.m_mysql_res_metadata = nullptr;
            other.m_mysql_stmt_handle_for_fetch = nullptr;
            other.m_current_sql_row = nullptr;
            other.m_is_valid = false;
            other.m_stmt_result_was_stored = false;
        }
        return *this;
    }

    bool MySqlTransportResult::isValid() const {
        return m_is_valid;
    }
    MySqlTransportError MySqlTransportResult::getError() const {
        return m_error_collector;
    }

    my_ulonglong MySqlTransportResult::getRowCount() const {
        return m_row_count;
    }
    unsigned int MySqlTransportResult::getFieldCount() const {
        return m_field_count;
    }

    const std::vector<MySqlTransportFieldMeta>& MySqlTransportResult::getFieldsMeta() const {
        return m_fields_meta;
    }

    std::optional<MySqlTransportFieldMeta> MySqlTransportResult::getFieldMeta(unsigned int col_idx) const {
        if (!m_is_valid || col_idx >= m_field_count) {
            return std::nullopt;
        }
        if (m_fields_meta.size() <= col_idx) return std::nullopt;
        return m_fields_meta[col_idx];
    }

    std::optional<MySqlTransportFieldMeta> MySqlTransportResult::getFieldMeta(const std::string& col_name) const {
        if (!m_is_valid) return std::nullopt;
        for (const auto& meta : m_fields_meta) {
            if (meta.name == col_name || meta.original_name == col_name) {
                return meta;
            }
        }
        return std::nullopt;
    }
    int MySqlTransportResult::getFieldIndex(const std::string& col_name) const {
        if (!m_is_valid) return -1;
        for (size_t i = 0; i < m_fields_meta.size(); ++i) {
            if (m_fields_meta[i].name == col_name || m_fields_meta[i].original_name == col_name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool MySqlTransportResult::fetchNextRow() {
        if (!m_is_valid) return false;
        clearCurrentRow();

        if (m_is_from_prepared_statement) {
            if (!m_mysql_stmt_handle_for_fetch || m_fetched_all_from_stmt) return false;

            int fetch_rc = mysql_stmt_fetch(m_mysql_stmt_handle_for_fetch);
            if (fetch_rc == 0) {
                m_current_row_idx++;
                return true;
            } else if (fetch_rc == MYSQL_NO_DATA) {
                m_fetched_all_from_stmt = true;
                return false;
            } else if (fetch_rc == MYSQL_DATA_TRUNCATED) {
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::DataError, "Data truncated during fetch.", mysql_stmt_errno(m_mysql_stmt_handle_for_fetch), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
                m_current_row_idx++;
                return true;
            } else {
                if (m_statement)
                    m_error_collector = m_statement->getError();
                else if (m_mysql_stmt_handle_for_fetch)
                    m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_fetch failed.", mysql_stmt_errno(m_mysql_stmt_handle_for_fetch), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
                else
                    m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_fetch failed (no statement context).");
                return false;
            }
        } else {
            if (!m_mysql_res_metadata) return false;
            m_current_sql_row = mysql_fetch_row(m_mysql_res_metadata);
            if (m_current_sql_row) {
                m_current_lengths = mysql_fetch_lengths(m_mysql_res_metadata);
                m_current_row_idx++;
                return true;
            } else {
                if (m_mysql_res_metadata && m_mysql_res_metadata->handle && mysql_errno(m_mysql_res_metadata->handle) == 0 && mysql_eof(m_mysql_res_metadata)) {
                    // Normal EOF
                } else if (m_mysql_res_metadata && m_mysql_res_metadata->handle) {
                    m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_fetch_row failed.", mysql_errno(m_mysql_res_metadata->handle), mysql_sqlstate(m_mysql_res_metadata->handle), mysql_error(m_mysql_res_metadata->handle));
                } else if (!m_error_collector.isOk()) {
                    // Error already set
                } else {
                    m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "Unknown error during mysql_fetch_row or no more rows.");
                }
                return false;
            }
        }
    }

    std::optional<mysql_protocol::MySqlNativeValue> MySqlTransportResult::getValue(unsigned int col_idx) {
        if (!m_is_valid || col_idx >= m_field_count) {
            m_error_collector = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "Invalid column index for getValue.");
            return std::nullopt;
        }
        if (m_fields_meta.size() <= col_idx) {
            m_error_collector = MySqlTransportError(MySqlTransportError::Category::InternalError, "Field metadata inconsistent with field count.");
            return std::nullopt;
        }

        if (m_is_from_prepared_statement) {
            if (!m_mysql_stmt_handle_for_fetch || m_current_row_idx == -1) {
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "No current row fetched or past end for prepared statement getValue.");
                return std::nullopt;
            }
            if (m_output_is_null_indicators.size() <= col_idx || m_output_bind_buffers.size() <= col_idx) {
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::InternalError, "Output bind/indicator buffers out of sync for prepared statement getValue.");
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
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::ProtocolError, "Failed to convert bound result to NativeValue: " + expected_nv.error().error_message, 0, nullptr, nullptr, expected_nv.error().error_code);
                return std::nullopt;
            }

        } else {
            if (!m_current_sql_row) {
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "No current row fetched for non-prepared statement getValue.");
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
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::InternalError, "Failed to fetch field info for getValue.");
                return std::nullopt;
            }
            if (!m_current_lengths) {
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::InternalError, "Row lengths not available for non-prepared getValue.");
                return std::nullopt;
            }

            auto expected_nv = mysql_protocol::mySqlRowFieldToNativeValue(m_current_sql_row[col_idx], m_current_lengths[col_idx], field_info);
            if (expected_nv) {
                return expected_nv.value();
            } else {
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::ProtocolError, "Failed to convert row field to NativeValue: " + expected_nv.error().error_message, 0, nullptr, nullptr, expected_nv.error().error_code);
                return std::nullopt;
            }
        }
    }
    std::optional<mysql_protocol::MySqlNativeValue> MySqlTransportResult::getValue(const std::string& col_name) {
        int idx = getFieldIndex(col_name);
        if (idx == -1) {
            m_error_collector = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "Invalid column name for getValue: " + col_name);
            return std::nullopt;
        }
        return getValue(static_cast<unsigned int>(idx));
    }

    bool MySqlTransportResult::isNull(unsigned int col_idx) {
        if (!m_is_valid || col_idx >= m_field_count) return true;

        if (m_is_from_prepared_statement) {
            if (!m_mysql_stmt_handle_for_fetch || m_current_row_idx == -1) return true;
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
        if (!m_is_valid || m_field_count == 0 || (m_is_from_prepared_statement && (m_current_row_idx == -1)) || (!m_is_from_prepared_statement && !m_current_sql_row)) {
            return row_values;
        }
        row_values.reserve(m_field_count);
        for (unsigned int i = 0; i < m_field_count; ++i) {
            auto val_opt = getValue(i);
            if (val_opt) {
                row_values.push_back(std::move(val_opt.value()));
            } else {
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

    void MySqlTransportResult::populateFieldsMeta() {
        if (m_meta_populated || !m_mysql_res_metadata || m_field_count == 0) {
            if (!m_mysql_res_metadata && m_field_count > 0 && m_is_valid) {  // m_is_valid check added
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::InternalError, "MYSQL_RES metadata handle is null in populateFieldsMeta when fields expected.");
                m_is_valid = false;  // Critical failure
            }
            return;
        }

        m_fields_meta.clear();
        m_fields_meta.resize(m_field_count);
        MYSQL_FIELD* fields_raw = mysql_fetch_fields(m_mysql_res_metadata);
        if (!fields_raw) {
            m_error_collector = MySqlTransportError(MySqlTransportError::Category::InternalError, "mysql_fetch_fields returned null.");
            m_field_count = 0;
            m_is_valid = false;
            return;
        }

        for (unsigned int i = 0; i < m_field_count; ++i) {
            m_fields_meta[i].name = fields_raw[i].name ? fields_raw[i].name : "";
            m_fields_meta[i].original_name = fields_raw[i].org_name ? fields_raw[i].org_name : "";
            m_fields_meta[i].table = fields_raw[i].table ? fields_raw[i].table : "";
            m_fields_meta[i].original_table = fields_raw[i].org_table ? fields_raw[i].org_table : "";
            m_fields_meta[i].db = fields_raw[i].db ? fields_raw[i].db : "";
            m_fields_meta[i].catalog = fields_raw[i].catalog ? fields_raw[i].catalog : "def";
            m_fields_meta[i].native_type_id = fields_raw[i].type;
            m_fields_meta[i].charsetnr = fields_raw[i].charsetnr;
            m_fields_meta[i].length = fields_raw[i].length;
            m_fields_meta[i].max_length = fields_raw[i].max_length;
            m_fields_meta[i].flags = fields_raw[i].flags;
            m_fields_meta[i].decimals = fields_raw[i].decimals;
        }
        m_meta_populated = true;
        // m_is_valid should already be true or set to false if errors occurred.
        // If we reach here without prior errors, it's valid.
    }

    void MySqlTransportResult::clearCurrentRow() {
        if (!m_is_from_prepared_statement) {
            m_current_sql_row = nullptr;
            m_current_lengths = nullptr;
        } else {
        }
    }

    void MySqlTransportResult::setupOutputBindBuffers() {
        if (!m_is_from_prepared_statement || m_field_count == 0 || !m_mysql_stmt_handle_for_fetch) return;
        if (m_fields_meta.size() != m_field_count) {
            m_error_collector = MySqlTransportError(MySqlTransportError::Category::InternalError, "Field metadata count mismatch in setupOutputBindBuffers.");
            m_is_valid = false;
            return;
        }

        m_output_bind_buffers.assign(m_field_count, MYSQL_BIND{});
        m_output_data_buffers.resize(m_field_count);

        // Per mysql.h, MYSQL_BIND::is_null and MYSQL_BIND::error are bool*.
        // We will use std::vector<char> to store 0 or 1, and then provide a pointer
        // to these char elements, reinterpret_cast-ed to bool*.
        // This relies on the common representation of bool as a byte where 0 is false
        // and non-zero (typically 1) is true, and that the MySQL C API handles this.
        m_output_is_null_indicators.assign(m_field_count, (char)0);
        m_output_length_indicators.assign(m_field_count, 0UL);
        m_output_error_indicators.assign(m_field_count, (char)0);

        for (unsigned int i = 0; i < m_field_count; ++i) {
            MYSQL_BIND& bind = m_output_bind_buffers[i];
            const MySqlTransportFieldMeta& meta = m_fields_meta[i];

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
                    if (buffer_sz == 0) buffer_sz = 66;
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
                    if (buffer_sz == 0) buffer_sz = 256;
                    break;
                default:
                    if (buffer_sz == 0) buffer_sz = 256;
                    break;
            }
            if (buffer_sz == 0) buffer_sz = 1;

            m_output_data_buffers[i].assign(buffer_sz, (unsigned char)0);  // Use unsigned char for data buffers
            bind.buffer = m_output_data_buffers[i].data();
            bind.buffer_length = buffer_sz;
            bind.length = &m_output_length_indicators[i];
            // Provide pointer to char, cast to bool*. MySQL C API expects this to work.
            bind.is_null = reinterpret_cast<bool*>(&m_output_is_null_indicators[i]);
            bind.error = reinterpret_cast<bool*>(&m_output_error_indicators[i]);
            bind.is_unsigned = (meta.flags & UNSIGNED_FLAG);
        }

        if (mysql_stmt_bind_result(m_mysql_stmt_handle_for_fetch, m_output_bind_buffers.data()) != 0) {
            if (m_statement)
                m_error_collector = m_statement->getError();
            else if (m_mysql_stmt_handle_for_fetch)
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_bind_result failed.", mysql_stmt_errno(m_mysql_stmt_handle_for_fetch), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
            else
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_bind_result failed (no statement context).");
            m_is_valid = false;
        }
    }

}  // namespace cpporm_mysql_transport