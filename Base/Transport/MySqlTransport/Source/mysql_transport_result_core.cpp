// cpporm_mysql_transport/mysql_transport_result_core.cpp
#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"

namespace cpporm_mysql_transport {

    MySqlTransportResult::MySqlTransportResult(MySqlTransportStatement* stmt, MYSQL_RES* meta_res_handle, const MySqlTransportError& initial_error)
        : m_statement(stmt),
          m_mysql_res_metadata(meta_res_handle),
          m_mysql_stmt_handle_for_fetch(stmt ? stmt->getNativeStatementHandle() : nullptr),
          m_error_collector_owned(initial_error),
          m_current_sql_row(nullptr),
          m_current_lengths(nullptr),
          m_meta_populated(false),
          m_is_valid(false),
          m_is_from_prepared_statement(true),
          m_stmt_result_was_stored(false),
          m_fetched_all_from_stmt(false) {
        // 如果 initial_error 已经是一个错误，我们可能不应该继续，或者至少要标记 m_is_valid = false
        if (!initial_error.isOk()) {
            if (m_mysql_res_metadata) {  // 即使有初始错误，也要释放传入的 meta_res_handle
                mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
            }
            m_is_valid = false;
            return;
        }

        if (!m_statement || !m_mysql_stmt_handle_for_fetch) {  // meta_res_handle 可以为 NULL 如果没有字段
            if (m_error_collector_owned.isOk()) {
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "Invalid statement or stmt_handle for MySqlTransportResult (prepared).");
            }
            if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
            m_mysql_res_metadata = nullptr;
            m_is_valid = false;
            return;
        }
        // 仅当 meta_res_handle 非空时才尝试 store_result (通常 SELECT 会返回元数据)
        // 如果 meta_res_handle 为空，说明查询没有返回列 (例如 DML 或 SELECT 1 WHERE FALSE)
        if (m_mysql_res_metadata) {
            if (mysql_stmt_store_result(m_mysql_stmt_handle_for_fetch) != 0) {
                // 尝试从语句句柄获取错误，因为它与 mysql_stmt_store_result 相关
                if (m_statement) {
                    unsigned int stmt_err_no = mysql_stmt_errno(m_mysql_stmt_handle_for_fetch);
                    if (stmt_err_no != 0) {  // 只有当确实有错误时才覆盖
                        m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, mysql_stmt_error(m_mysql_stmt_handle_for_fetch), static_cast<int>(stmt_err_no), mysql_stmt_sqlstate(m_mysql_stmt_handle_for_fetch), mysql_stmt_error(m_mysql_stmt_handle_for_fetch));
                    } else if (m_error_collector_owned.isOk()) {  // 如果语句错误码为0，但操作失败
                        m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_store_result failed but statement reports no error.");
                    }
                } else if (m_error_collector_owned.isOk()) {  // 理论上不应发生，因为 m_statement 必须存在
                    m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::QueryError, "mysql_stmt_store_result failed (no statement context).");
                }
                mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                m_stmt_result_was_stored = false;
                m_is_valid = false;
                return;
            }
            m_stmt_result_was_stored = true;
            m_row_count = mysql_stmt_num_rows(m_mysql_stmt_handle_for_fetch);
            m_field_count = mysql_num_fields(m_mysql_res_metadata);
        } else {                                                                    // meta_res_handle 为 NULL (例如，没有列的查询)
            m_row_count = mysql_stmt_affected_rows(m_mysql_stmt_handle_for_fetch);  // 对于无结果集的语句，这更有意义
            m_field_count = 0;
            m_stmt_result_was_stored = false;  // 没有结果可存储
        }

        if (m_field_count > 0 && m_mysql_res_metadata) {  // 只有在有字段且元数据存在时才处理字段元数据
            populateFieldsMeta();
            if (!m_error_collector_owned.isOk()) {  // populateFieldsMeta 可能会设置错误
                m_is_valid = false;                 // 确保 m_is_valid 更新
                // 清理，因为 populateFieldsMeta 失败
                if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                if (m_stmt_result_was_stored && m_mysql_stmt_handle_for_fetch) {
                    mysql_stmt_free_result(m_mysql_stmt_handle_for_fetch);
                }
                m_stmt_result_was_stored = false;
                return;
            }
            if (!m_fields_meta.empty()) {
                setupOutputBindBuffers();
                if (!m_error_collector_owned.isOk()) {  // setupOutputBindBuffers 可能会设置错误
                    m_is_valid = false;
                    // 清理
                    if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
                    m_mysql_res_metadata = nullptr;
                    if (m_stmt_result_was_stored && m_mysql_stmt_handle_for_fetch) {
                        mysql_stmt_free_result(m_mysql_stmt_handle_for_fetch);
                    }
                    m_stmt_result_was_stored = false;
                    return;
                }
            } else {                                   // m_fields_meta 为空但 m_field_count > 0，这是个问题
                if (m_error_collector_owned.isOk()) {  // 如果还没有错误
                    m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::InternalError, "Field count > 0 but no field metadata populated.");
                }
                m_is_valid = false;
                if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                if (m_stmt_result_was_stored && m_mysql_stmt_handle_for_fetch) {
                    mysql_stmt_free_result(m_mysql_stmt_handle_for_fetch);
                }
                m_stmt_result_was_stored = false;
                return;
            }
        }
        m_is_valid = true;  // 如果所有步骤都成功
    }

    MySqlTransportResult::MySqlTransportResult(MYSQL_RES* stored_res_handle, const MySqlTransportError& initial_error)
        : m_statement(nullptr),
          m_mysql_res_metadata(stored_res_handle),
          m_mysql_stmt_handle_for_fetch(nullptr),
          m_error_collector_owned(initial_error),
          m_current_sql_row(nullptr),
          m_current_lengths(nullptr),
          m_meta_populated(false),
          m_is_valid(false),
          m_is_from_prepared_statement(false),
          m_stmt_result_was_stored(false),
          m_fetched_all_from_stmt(false) {
        if (!initial_error.isOk()) {
            if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
            m_mysql_res_metadata = nullptr;
            m_is_valid = false;
            return;
        }

        if (!m_mysql_res_metadata) {
            if (m_error_collector_owned.isOk()) {  // 仅当没有初始错误时才设置新错误
                m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::ApiUsageError, "Null MYSQL_RES handle passed to MySqlTransportResult constructor (non-prepared).");
            }
            m_is_valid = false;
            return;
        }
        m_row_count = mysql_num_rows(m_mysql_res_metadata);
        m_field_count = mysql_num_fields(m_mysql_res_metadata);

        if (m_field_count > 0) {
            populateFieldsMeta();
            if (!m_error_collector_owned.isOk()) {
                m_is_valid = false;
                if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
                return;
            }
            if (m_fields_meta.empty()) {  // 如果 populateFieldsMeta 成功但 m_fields_meta 为空
                if (m_error_collector_owned.isOk()) {
                    m_error_collector_owned = MySqlTransportError(MySqlTransportError::Category::InternalError, "Field count > 0 but no field metadata populated (non-prepared).");
                }
                m_is_valid = false;
                if (m_mysql_res_metadata) mysql_free_result(m_mysql_res_metadata);
                m_mysql_res_metadata = nullptr;
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
          m_error_collector_owned(std::move(other.m_error_collector_owned)),
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
            m_error_collector_owned = std::move(other.m_error_collector_owned);
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
        return m_error_collector_owned;
    }

    my_ulonglong MySqlTransportResult::getRowCount() const {
        return m_row_count;
    }
    unsigned int MySqlTransportResult::getFieldCount() const {
        return m_field_count;
    }

}  // namespace cpporm_mysql_transport