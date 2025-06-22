// SqlDriver/Source/mysql/mysql_specific_result.cpp
#include "sqldriver/mysql/mysql_specific_result.h"

#include <stdexcept>  // For std::logic_error

#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "cpporm_mysql_transport/mysql_transport_types.h"  // For MySqlTransportBindParam
#include "sqldriver/mysql/mysql_driver_helper.h"           // For converters and NamedPlaceholderInfo
#include "sqldriver/mysql/mysql_specific_driver.h"

namespace cpporm_sqldriver {

    MySqlSpecificResult::MySqlSpecificResult(const MySqlSpecificDriver* driver)
        : m_driver(driver),
          m_transport_statement(nullptr),
          m_transport_result_set(nullptr),
          m_current_row_index(-1),
          m_num_rows_affected_cache(0),
          m_is_active_flag(false),
          m_precision_policy(NumericalPrecisionPolicy::LowPrecision),  // 直接使用，因为它在 cpporm_sqldriver 命名空间中定义
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
            finish();  // 清理当前资源

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
            // 只有在之前没有错误时才设置，以避免覆盖更具体的错误
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
            // 假设 MySqlTransportConnection::createStatement 失败时会在连接上设置错误
            m_last_error_cache = mysql_helper::transportErrorToSqlError(m_driver->getTransportConnection()->getLastError());
            if (m_last_error_cache.category() == ErrorCategory::NoError) {  // 如果连接上没有特定错误
                m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Failed to create transport statement.", "prepare");
            }
            return false;
        }

        bool success = m_transport_statement->prepare();
        updateLastErrorCacheFromTransportStatement();
        m_is_active_flag = success;
        return success;
    }

    bool MySqlSpecificResult::applyBindingsToTransportStatement() {
        if (!m_transport_statement || !m_transport_statement->isPrepared()) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Statement not prepared for binding.", "applyBindingsToTransportStatement");
            return false;
        }
        m_ordered_transport_bind_params.clear();

        if (m_placeholder_info.hasNamedPlaceholders) {
            m_ordered_transport_bind_params.reserve(m_placeholder_info.orderedParamNames.size());
            for (const std::string& name : m_placeholder_info.orderedParamNames) {
                auto it = m_named_bind_values_map.find(name);
                if (it == m_named_bind_values_map.end()) {
                    m_last_error_cache = SqlError(ErrorCategory::Syntax, "Named parameter ':" + name + "' used in query but not bound.", "applyBindings");
                    return false;
                }
                m_ordered_transport_bind_params.emplace_back(mysql_helper::sqlValueToMySqlNativeValue(it->second));
            }
        } else {
            m_ordered_transport_bind_params.reserve(m_positional_bind_values.size());
            for (const auto& sql_val : m_positional_bind_values) {
                m_ordered_transport_bind_params.emplace_back(mysql_helper::sqlValueToMySqlNativeValue(sql_val));
            }
        }
        // MySqlTransportBindParam 的构造函数现在接受 MySqlNativeValue
        bool success = m_transport_statement->bindParams(m_ordered_transport_bind_params);
        if (!success) updateLastErrorCacheFromTransportStatement();
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

        std::optional<my_ulonglong> affected_opt = m_transport_statement->execute();
        if (affected_opt) {
            m_num_rows_affected_cache = *affected_opt;
            // MySqlTransportStatement::getLastInsertId() 返回 my_ulonglong
            // MySqlNativeValue 构造函数需要能够处理 uint64_t (my_ulonglong 通常是这个类型)
            mysql_protocol::MySqlNativeValue last_id_native;
            last_id_native.data = static_cast<uint64_t>(m_transport_statement->getLastInsertId());
            last_id_native.original_mysql_type = MYSQL_TYPE_LONGLONG;  // 假设 ID 是 LONGLONG
            last_id_native.original_mysql_flags = UNSIGNED_FLAG;       // 通常自增 ID 是无符号的
            m_last_insert_id_cache = mysql_helper::mySqlNativeValueToSqlValue(last_id_native);

            if (m_driver->getTransportConnection()->getNativeHandle() && m_transport_statement->getNativeStatementHandle() &&  // 确保语句句柄有效
                mysql_stmt_field_count(m_transport_statement->getNativeStatementHandle()) > 0) {
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
        } else {
            updateLastErrorCacheFromTransportStatement();
            m_is_active_flag = false;
            return false;
        }
    }

    void MySqlSpecificResult::cleanupAfterExecution(bool retain_result_set) {
        m_current_row_index = -1;
        m_current_record_buffer_cache.clear();
        // DML 操作后 m_num_rows_affected_cache 和 m_last_insert_id_cache 应该保留
        // 直到下一次 exec。 cleanupAfterExecution 主要清理结果集相关的状态。
        // 所以，不重置 m_num_rows_affected_cache 和 m_last_insert_id_cache。

        if (!retain_result_set) {
            m_transport_result_set.reset();
        }
    }

    bool MySqlSpecificResult::ensureResultSet() {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            return true;
        }
        if (m_transport_statement && m_transport_statement->isPrepared() && m_is_active_flag) {
            // 如果语句是活动的，并且没有结果集，尝试执行 executeQuery 获取结果集
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
        // 修正 ErrorCategory
        m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "No valid result set available or statement not a query.", "ensureResultSet");
        return false;
    }

    void MySqlSpecificResult::addPositionalBindValue(const SqlValue& value, ParamType /*type*/) {
        m_positional_bind_values.push_back(value);
    }

    void MySqlSpecificResult::setNamedBindValue(const std::string& placeholder, const SqlValue& value, ParamType /*type*/) {
        std::string clean_placeholder = placeholder;
        if (!placeholder.empty() && (placeholder[0] == ':' || placeholder[0] == '@')) {
            clean_placeholder = placeholder.substr(1);
        }
        m_named_bind_values_map[clean_placeholder] = value;
    }

    void MySqlSpecificResult::clearBindValues() {
        m_positional_bind_values.clear();
        m_named_bind_values_map.clear();
        m_ordered_transport_bind_params.clear();
    }

    bool MySqlSpecificResult::fetchNext(SqlRecord& record_buffer) {
        if (!ensureResultSet()) return false;
        clearLastErrorCache();
        record_buffer.clear();

        if (m_transport_result_set->fetchNextRow()) {
            m_current_row_index++;
            const auto& fields_meta_transport = m_transport_result_set->getFieldsMeta();
            for (unsigned int i = 0; i < fields_meta_transport.size(); ++i) {
                SqlField sql_driver_field = mysql_helper::metaToSqlField(fields_meta_transport[i]);
                auto native_val_opt = m_transport_result_set->getValue(i);
                if (native_val_opt) {
                    sql_driver_field.setValue(mysql_helper::mySqlNativeValueToSqlValue(*native_val_opt));
                } else {
                    sql_driver_field.setValue(SqlValue());  // Null
                }
                record_buffer.append(sql_driver_field);
            }
            m_current_record_buffer_cache = record_buffer;
            return true;
        } else {
            updateLastErrorCacheFromTransportResult();
            m_current_row_index = -1;
            m_current_record_buffer_cache.clear();
            return false;
        }
    }

    SqlValue MySqlSpecificResult::data(int column_index) {
        if (!ensureResultSet()) return SqlValue();
        if (m_current_row_index < 0 || column_index < 0 || static_cast<unsigned int>(column_index) >= (m_transport_result_set ? m_transport_result_set->getFieldCount() : 0)) {
            m_last_error_cache = SqlError(ErrorCategory::DataRelated, "Invalid index or no current row for data().", "data");  // 使用 DataRelated
            return SqlValue();
        }
        clearLastErrorCache();
        auto native_val_opt = m_transport_result_set->getValue(static_cast<unsigned int>(column_index));
        if (native_val_opt) {
            return mysql_helper::mySqlNativeValueToSqlValue(*native_val_opt);
        } else {
            updateLastErrorCacheFromTransportResult();
            return SqlValue();
        }
    }

    bool MySqlSpecificResult::isNull(int column_index) {
        if (!ensureResultSet()) return true;
        if (m_current_row_index < 0 || column_index < 0 || static_cast<unsigned int>(column_index) >= (m_transport_result_set ? m_transport_result_set->getFieldCount() : 0)) {
            return true;
        }
        clearLastErrorCache();
        bool is_null_val = m_transport_result_set->isNull(static_cast<unsigned int>(column_index));
        // isNull 本身不太可能在 transport 层产生新错误，除非状态非法
        if (!m_transport_result_set->getError().isOk()) {
            updateLastErrorCacheFromTransportResult();
        }
        return is_null_val;
    }

    SqlRecord MySqlSpecificResult::recordMetadata() const {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            return mysql_helper::metasToSqlRecord(m_transport_result_set->getFieldsMeta());
        }
        return SqlRecord();
    }

    long long MySqlSpecificResult::numRowsAffected() {
        return static_cast<long long>(m_num_rows_affected_cache);
    }

    SqlValue MySqlSpecificResult::lastInsertId() {
        return m_last_insert_id_cache;
    }

    int MySqlSpecificResult::columnCount() const {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            return static_cast<int>(m_transport_result_set->getFieldCount());
        }
        return 0;
    }

    int MySqlSpecificResult::size() {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            return static_cast<int>(m_transport_result_set->getRowCount());
        }
        return -1;
    }

    int MySqlSpecificResult::at() const {
        return static_cast<int>(m_current_row_index);
    }

    bool MySqlSpecificResult::isActive() const {
        return m_is_active_flag;
    }

    bool MySqlSpecificResult::isValid() const {
        // 一个结果集要有效，它必须是活动的，并且 transport 结果集也必须是有效的
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

    bool MySqlSpecificResult::setQueryTimeout(int /*seconds*/) {
        return false;
    }
    bool MySqlSpecificResult::setNumericalPrecisionPolicy(NumericalPrecisionPolicy policy) {
        m_precision_policy = policy;
        return true;
    }
    bool MySqlSpecificResult::setPrefetchSize(int rows) {
        m_prefetch_size_hint = rows;
        return false;
    }
    int MySqlSpecificResult::prefetchSize() const {
        return m_prefetch_size_hint;
    }
    void MySqlSpecificResult::bindBlobStream(int /*pos*/, std::shared_ptr<std::istream> /*stream*/, long long /*size*/, ParamType /*type*/) { /* TODO */
    }
    void MySqlSpecificResult::bindBlobStream(const std::string& /*placeholder*/, std::shared_ptr<std::istream> /*stream*/, long long /*size*/, ParamType /*type*/) { /* TODO */
    }
    void MySqlSpecificResult::reset() {
        if (m_transport_statement && m_transport_statement->getNativeStatementHandle()) {
            if (mysql_stmt_reset(m_transport_statement->getNativeStatementHandle()) == 0) {
                cleanupAfterExecution(false);
                m_is_active_flag = m_transport_statement->isPrepared();
                // 清除绑定值，因为 mysql_stmt_reset 后它们不再有效，需要重新绑定
                clearBindValues();
                return;
            } else {
                updateLastErrorCacheFromTransportStatement();
            }
        }
        m_is_active_flag = false;
    }
    bool MySqlSpecificResult::setForwardOnly(bool forward) {
        if (forward) m_scroll_mode_hint = SqlResultNs::ScrollMode::ForwardOnly;
        // 实际的滚动能力取决于MySQL和驱动的实现，通常是仅向前的
        return forward;
    }
    bool MySqlSpecificResult::fetchPrevious(SqlRecord& /*record_buffer*/) {
        // MySQL C API + store_result 后，可以使用 mysql_stmt_data_seek 模拟
        // 但这会增加复杂性。目前返回 false。
        return false;
    }
    bool MySqlSpecificResult::fetchFirst(SqlRecord& /*record_buffer*/) {
        // 同上
        return false;
    }
    bool MySqlSpecificResult::fetchLast(SqlRecord& /*record_buffer*/) {
        // 同上
        return false;
    }
    bool MySqlSpecificResult::fetch(int /*index*/, SqlRecord& /*record_buffer*/, CursorMovement /*movement*/) {
        // 同上
        return false;
    }
    std::shared_ptr<std::istream> MySqlSpecificResult::openReadableBlobStream(int /*column_index*/) {
        return nullptr;
    }
    std::shared_ptr<std::ostream> MySqlSpecificResult::openWritableBlobStream(int /*column_index*/, long long /*initial_size_hint*/) {
        return nullptr;
    }
    SqlRecord MySqlSpecificResult::currentFetchedRow() const {
        return m_current_record_buffer_cache;
    }
    SqlField MySqlSpecificResult::field(int column_index) const {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            auto transport_field_meta_opt = m_transport_result_set->getFieldMeta(static_cast<unsigned int>(column_index));
            if (transport_field_meta_opt) {
                return mysql_helper::metaToSqlField(*transport_field_meta_opt);
            }
        }
        return SqlField();
    }

    bool MySqlSpecificResult::nextResult() {
        if (!m_transport_statement || !m_transport_statement->getNativeStatementHandle()) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Statement handle not available for nextResult.", "nextResult");
            return false;
        }
        cleanupAfterExecution(false);
        clearLastErrorCache();

        int status = mysql_stmt_next_result(m_transport_statement->getNativeStatementHandle());
        if (status == 0) {             // 更多结果存在
            m_current_row_index = -1;  // 重置行索引
            m_num_rows_affected_cache = mysql_stmt_affected_rows(m_transport_statement->getNativeStatementHandle());
            // 检查这个新结果集是否有列
            if (m_driver->getTransportConnection()->getNativeHandle() && mysql_stmt_field_count(m_transport_statement->getNativeStatementHandle()) > 0) {
                m_transport_result_set = m_transport_statement->executeQuery();  // 这会获取新的元数据和结果
                if (!m_transport_result_set || !m_transport_result_set->isValid()) {
                    updateLastErrorCacheFromTransportResult();
                    m_is_active_flag = false;
                    return false;
                }
            } else {
                m_transport_result_set.reset();  // 这个结果没有列 (例如，存储过程中的 DML 语句)
            }
            m_is_active_flag = true;
            return true;
        } else if (status == -1) {  // 没有更多结果了
            m_is_active_flag = false;
            // 检查 mysql_stmt_errno 是否为0，以确认这确实是 "no more results" 而不是一个错误
            if (mysql_stmt_errno(m_transport_statement->getNativeStatementHandle()) != 0) {
                updateLastErrorCacheFromTransportStatement();  // 如果有错误，则更新
            }
            return false;
        } else {  // 出错 (status > 0)
            updateLastErrorCacheFromTransportStatement();
            m_is_active_flag = false;
            return false;
        }
    }
    SqlValue MySqlSpecificResult::getOutParameter(int /*pos*/) const {
        return SqlValue();
    }
    SqlValue MySqlSpecificResult::getOutParameter(const std::string& /*name*/) const {
        return SqlValue();
    }
    std::map<std::string, SqlValue> MySqlSpecificResult::getAllOutParameters() const {
        return {};
    }

    bool MySqlSpecificResult::setNamedBindingSyntax(SqlResultNs::NamedBindingSyntax syntax) {
        m_named_binding_syntax = syntax;
        return true;
    }

}  // namespace cpporm_sqldriver