#include <stdexcept>

#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "sqldriver/mysql/mysql_driver_helper.h"
#include "sqldriver/mysql/mysql_specific_driver.h"
#include "sqldriver/mysql/mysql_specific_result.h"

namespace cpporm_sqldriver {

    MySqlSpecificResult::MySqlSpecificResult(const MySqlSpecificDriver* driver)
        : m_driver(driver),
          m_transport_statement(nullptr),
          m_transport_result_set(nullptr),
          m_current_row_index(-1),
          m_num_rows_affected_cache(0),
          m_is_active_flag(false),
          m_precision_policy(NumericalPrecisionPolicy::LowPrecision),
          m_named_binding_syntax(SqlResultNs::NamedBindingSyntax::Colon),
          m_scroll_mode_hint(SqlResultNs::ScrollMode::ForwardOnly),
          m_prefetch_size_hint(0) {
        if (!m_driver || !m_driver->getTransportConnection()) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "MySqlSpecificResult: Invalid driver or transport connection.", "Constructor");
        }
    }

    MySqlSpecificResult::~MySqlSpecificResult() {
        finish();
    }

    MySqlSpecificResult::MySqlSpecificResult(MySqlSpecificResult&& other) noexcept
        : m_driver(other.m_driver),
          m_transport_statement(std::move(other.m_transport_statement)),
          m_transport_result_set(std::move(other.m_transport_result_set)),
          m_original_query_text(std::move(other.m_original_query_text)),
          m_placeholder_info(std::move(other.m_placeholder_info)),
          m_positional_bind_values(std::move(other.m_positional_bind_values)),
          m_named_bind_values_map(std::move(other.m_named_bind_values_map)),
          m_ordered_transport_bind_params(std::move(other.m_ordered_transport_bind_params)),
          m_current_record_buffer_cache(std::move(other.m_current_record_buffer_cache)),
          m_current_row_index(other.m_current_row_index),
          m_num_rows_affected_cache(other.m_num_rows_affected_cache),
          m_last_insert_id_cache(std::move(other.m_last_insert_id_cache)),
          m_last_error_cache(std::move(other.m_last_error_cache)),
          m_is_active_flag(other.m_is_active_flag),
          m_precision_policy(other.m_precision_policy),
          m_named_binding_syntax(other.m_named_binding_syntax),
          m_scroll_mode_hint(other.m_scroll_mode_hint),
          m_prefetch_size_hint(other.m_prefetch_size_hint) {
        other.m_driver = nullptr;
        other.m_is_active_flag = false;
        other.m_current_row_index = -1;
    }

    MySqlSpecificResult& MySqlSpecificResult::operator=(MySqlSpecificResult&& other) noexcept {
        if (this != &other) {
            finish();

            m_driver = other.m_driver;
            m_transport_statement = std::move(other.m_transport_statement);
            m_transport_result_set = std::move(other.m_transport_result_set);
            m_original_query_text = std::move(other.m_original_query_text);
            m_placeholder_info = std::move(other.m_placeholder_info);
            m_positional_bind_values = std::move(other.m_positional_bind_values);
            m_named_bind_values_map = std::move(other.m_named_bind_values_map);
            m_ordered_transport_bind_params = std::move(other.m_ordered_transport_bind_params);
            m_current_record_buffer_cache = std::move(other.m_current_record_buffer_cache);
            m_current_row_index = other.m_current_row_index;
            m_num_rows_affected_cache = other.m_num_rows_affected_cache;
            m_last_insert_id_cache = std::move(other.m_last_insert_id_cache);
            m_last_error_cache = std::move(other.m_last_error_cache);
            m_is_active_flag = other.m_is_active_flag;
            m_precision_policy = other.m_precision_policy;
            m_named_binding_syntax = other.m_named_binding_syntax;
            m_scroll_mode_hint = other.m_scroll_mode_hint;
            m_prefetch_size_hint = other.m_prefetch_size_hint;

            other.m_driver = nullptr;
            other.m_is_active_flag = false;
            other.m_current_row_index = -1;
        }
        return *this;
    }

    void MySqlSpecificResult::updateLastErrorCacheFromTransportStatement() {
        if (m_transport_statement) {
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_statement->getError());
        } else {
            if (m_last_error_cache.category() == ErrorCategory::NoError) {
                m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Transport statement is null.", "updateLastErrorCacheFromTransportStatement");
            }
        }
    }

    void MySqlSpecificResult::updateLastErrorCacheFromTransportResult() {
        if (m_transport_result_set) {
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_result_set->getError());
        } else if (m_transport_statement && !m_transport_statement->getError().isOk()) {
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_transport_statement->getError());
        } else {
            if (m_last_error_cache.category() == ErrorCategory::NoError) {
                m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Transport result set is null and no prior statement error.", "updateLastErrorCacheFromTransportResult");
            }
        }
    }

    void MySqlSpecificResult::clearLastErrorCache() {
        m_last_error_cache = SqlError();
    }

    void MySqlSpecificResult::cleanupAfterExecution(bool retain_result_set) {
        m_current_row_index = -1;
        m_current_record_buffer_cache.clear();
        if (!retain_result_set) {
            m_transport_result_set.reset();
        }
    }

    bool MySqlSpecificResult::ensureResultSet() {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            return true;
        }
        if (m_transport_statement && m_transport_statement->isPrepared() && m_is_active_flag) {
            if (m_driver->getTransportConnection()->getNativeHandle() && m_transport_statement->getNativeStatementHandle() && mysql_stmt_field_count(m_transport_statement->getNativeStatementHandle()) > 0) {
                m_transport_result_set = m_transport_statement->executeQuery();
                if (m_transport_result_set && m_transport_result_set->isValid()) {
                    return true;
                } else {
                    updateLastErrorCacheFromTransportResult();
                    return false;
                }
            }
        }
        m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "No valid result set available or statement not a query.", "ensureResultSet");
        return false;
    }

    bool MySqlSpecificResult::isActive() const {
        return m_is_active_flag;
    }

    bool MySqlSpecificResult::isValid() const {
        return m_is_active_flag && m_transport_result_set && m_transport_result_set->isValid();
    }

    SqlError MySqlSpecificResult::error() const {
        return m_last_error_cache;
    }

    const std::string& MySqlSpecificResult::lastQuery() const {
        return m_original_query_text;
    }

    const std::string& MySqlSpecificResult::preparedQueryText() const {
        return m_placeholder_info.processedQuery;
    }

    void MySqlSpecificResult::finish() {
        cleanupAfterExecution(false);
        if (m_transport_statement) {
            m_transport_statement->close();
        }
        m_is_active_flag = false;
        m_positional_bind_values.clear();
        m_named_bind_values_map.clear();
        m_ordered_transport_bind_params.clear();
    }

    void MySqlSpecificResult::clear() {
        finish();
        m_original_query_text.clear();
        m_placeholder_info = mysql_helper::NamedPlaceholderInfo();
    }

    void MySqlSpecificResult::reset() {
        if (m_transport_statement && m_transport_statement->getNativeStatementHandle()) {
            if (mysql_stmt_reset(m_transport_statement->getNativeStatementHandle()) == 0) {
                cleanupAfterExecution(false);
                m_is_active_flag = m_transport_statement->isPrepared();
                clearBindValues();
                return;
            } else {
                updateLastErrorCacheFromTransportStatement();
            }
        }
        m_is_active_flag = false;
    }

}  // namespace cpporm_sqldriver