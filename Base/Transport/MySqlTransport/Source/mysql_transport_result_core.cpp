// cpporm_mysql_transport/mysql_transport_result_core.cpp
#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
// #include <cstring> // Not needed here anymore

// Forward declarations for functions defined in other _result_....cpp files
// void MySqlTransportResult::populateFieldsMeta(); // Declared in header
// void MySqlTransportResult::setupOutputBindBuffers(); // Declared in header
// void MySqlTransportResult::clearCurrentRow(); // Declared in header

namespace cpporm_mysql_transport {

    MySqlTransportResult::MySqlTransportResult(MySqlTransportStatement* stmt, MYSQL_RES* meta_res_handle, MySqlTransportError& err_ref)
        : m_statement(stmt),
          m_mysql_res_metadata(meta_res_handle),
          m_mysql_stmt_handle_for_fetch(stmt ? stmt->getNativeStatementHandle() : nullptr),
          m_error_collector(err_ref),
          m_current_sql_row(nullptr),
          m_current_lengths(nullptr),
          m_meta_populated(false),
          m_is_valid(false),
          m_is_from_prepared_statement(true),
          m_stmt_result_was_stored(false),
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
            m_stmt_result_was_stored = false;
            return;
        }
        m_stmt_result_was_stored = true;

        m_row_count = mysql_stmt_num_rows(m_mysql_stmt_handle_for_fetch);
        m_field_count = mysql_num_fields(m_mysql_res_metadata);

        if (m_field_count > 0) {
            populateFieldsMeta();
            if (!m_fields_meta.empty() && m_is_valid) {
                setupOutputBindBuffers();
            } else if (m_is_valid) {
                mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                if (m_stmt_result_was_stored && m_mysql_stmt_handle_for_fetch) {
                    mysql_stmt_free_result(m_mysql_stmt_handle_for_fetch);
                }
                m_stmt_result_was_stored = false;
                m_is_valid = false;
                return;
            } else {
                if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                if (m_stmt_result_was_stored && m_mysql_stmt_handle_for_fetch) {
                    mysql_stmt_free_result(m_mysql_stmt_handle_for_fetch);
                }
                m_stmt_result_was_stored = false;
                return;
            }
        }
        if (m_field_count == 0 || (m_field_count > 0 && m_is_valid)) {
            m_is_valid = true;
        }
    }

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
            if (!m_is_valid) {
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

    // getNativeMetadataHandle() is defined inline in the header, remove from .cpp
    // MYSQL_RES* MySqlTransportResult::getNativeMetadataHandle() const {
    //     return m_mysql_res_metadata;
    // }

    // getNativeStatementHandleForFetch() is defined inline in the header, remove from .cpp
    // MYSQL_STMT* MySqlTransportResult::getNativeStatementHandleForFetch() const {
    //     return m_mysql_stmt_handle_for_fetch;
    // }

}  // namespace cpporm_mysql_transport