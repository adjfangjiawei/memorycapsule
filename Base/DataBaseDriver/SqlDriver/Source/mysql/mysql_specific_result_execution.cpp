#include <algorithm>
#include <cctype>

#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "sqldriver/mysql/mysql_driver_helper.h"
#include "sqldriver/mysql/mysql_specific_driver.h"
#include "sqldriver/mysql/mysql_specific_result.h"

namespace cpporm_sqldriver {

    bool MySqlSpecificResult::prepare(const std::string& query, const std::map<std::string, SqlValueType>* /*named_bindings_type_hints*/, SqlResultNs::ScrollMode scroll, SqlResultNs::ConcurrencyMode /*concur*/) {
        if (!m_driver || !m_driver->getTransportConnection()) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Driver or transport connection not available for prepare.", "prepare");
            return false;
        }
        finish();
        clearLastErrorCache();

        m_original_query_text = query;
        m_scroll_mode_hint = scroll;

        if (m_named_binding_syntax != SqlResultNs::NamedBindingSyntax::QuestionMark) {
            m_placeholder_info = mysql_helper::processQueryForPlaceholders(m_original_query_text, m_named_binding_syntax);
        } else {
            m_placeholder_info.processedQuery = m_original_query_text;
            m_placeholder_info.hasNamedPlaceholders = false;
            m_placeholder_info.orderedParamNames.clear();
            m_placeholder_info.nameToIndicesMap.clear();
        }

        m_transport_statement = m_driver->getTransportConnection()->createStatement(m_placeholder_info.processedQuery);
        if (!m_transport_statement) {
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_driver->getTransportConnection()->getLastError());
            if (m_last_error_cache.category() == ErrorCategory::NoError) {
                m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Failed to create transport statement.", "prepare");
            }
            return false;
        }

        bool success = m_transport_statement->prepare();
        updateLastErrorCacheFromTransportStatement();
        m_is_active_flag = success;
        return success;
    }

    bool MySqlSpecificResult::exec() {
        if (!m_transport_statement) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Statement not initialized for exec.", "exec");
            return false;
        }
        if (!m_transport_statement->isPrepared()) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Statement not prepared for exec.", "exec");
            return false;
        }
        cleanupAfterExecution(false);
        clearLastErrorCache();

        if (!applyBindingsToTransportStatement()) {
            return false;
        }

        bool is_query_type = m_transport_statement->isUtilityCommand();
        if (!is_query_type) {
            std::string upper_query = m_original_query_text;
            std::transform(upper_query.begin(), upper_query.end(), upper_query.begin(), [](unsigned char c) {
                return std::toupper(c);
            });
            if (upper_query.rfind("SELECT", 0) == 0) {
                is_query_type = true;
            }
        }

        if (is_query_type) {
            m_transport_result_set = m_transport_statement->executeQuery();
            if (!m_transport_result_set || !m_transport_result_set->isValid()) {
                updateLastErrorCacheFromTransportStatement();
                m_is_active_flag = false;
                return false;
            }
            m_num_rows_affected_cache = m_transport_result_set->getRowCount();
            m_last_insert_id_cache = mysql_helper::mySqlNativeValueToSqlValue({});
        } else {
            std::optional<my_ulonglong> affected_opt = m_transport_statement->execute();
            if (!affected_opt) {
                updateLastErrorCacheFromTransportStatement();
                m_is_active_flag = false;
                return false;
            }
            m_num_rows_affected_cache = *affected_opt;

            mysql_protocol::MySqlNativeValue last_id_native;
            last_id_native.data = static_cast<uint64_t>(m_transport_statement->getLastInsertId());
            last_id_native.original_mysql_type = MYSQL_TYPE_LONGLONG;
            last_id_native.original_mysql_flags = UNSIGNED_FLAG;
            m_last_insert_id_cache = mysql_helper::mySqlNativeValueToSqlValue(last_id_native);

            if (m_driver->getTransportConnection()->getNativeHandle() && m_transport_statement->getNativeStatementHandle() && mysql_stmt_field_count(m_transport_statement->getNativeStatementHandle()) > 0) {
                m_transport_result_set = m_transport_statement->executeQuery();
                if (!m_transport_result_set || !m_transport_result_set->isValid()) {
                    updateLastErrorCacheFromTransportResult();
                    m_is_active_flag = false;
                    return false;
                }
            } else {
                m_transport_result_set.reset();
            }
        }
        m_is_active_flag = true;
        return true;
    }

    bool MySqlSpecificResult::nextResult() {
        if (!m_transport_statement || !m_transport_statement->getNativeStatementHandle()) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Statement handle not available for nextResult.", "nextResult");
            return false;
        }
        cleanupAfterExecution(false);
        clearLastErrorCache();

        int status = mysql_stmt_next_result(m_transport_statement->getNativeStatementHandle());
        if (status == 0) {
            m_current_row_index = -1;
            m_num_rows_affected_cache = mysql_stmt_affected_rows(m_transport_statement->getNativeStatementHandle());
            if (m_driver->getTransportConnection()->getNativeHandle() && mysql_stmt_field_count(m_transport_statement->getNativeStatementHandle()) > 0) {
                m_transport_result_set = m_transport_statement->executeQuery();
                if (!m_transport_result_set || !m_transport_result_set->isValid()) {
                    updateLastErrorCacheFromTransportResult();
                    m_is_active_flag = false;
                    return false;
                }
            } else {
                m_transport_result_set.reset();
            }
            m_is_active_flag = true;
            return true;
        } else if (status == -1) {
            m_is_active_flag = false;
            if (mysql_stmt_errno(m_transport_statement->getNativeStatementHandle()) != 0) {
                updateLastErrorCacheFromTransportStatement();
            }
            return false;
        } else {
            updateLastErrorCacheFromTransportStatement();
            m_is_active_flag = false;
            return false;
        }
    }

}  // namespace cpporm_sqldriver