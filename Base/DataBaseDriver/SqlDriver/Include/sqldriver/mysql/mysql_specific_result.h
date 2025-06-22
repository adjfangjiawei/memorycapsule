// SqlDriver/Include/sqldriver/mysql/mysql_specific_result.h
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_types.h"  // 包含 MySqlTransportBindParam 的定义
#include "sqldriver/mysql/mysql_driver_helper.h"
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_result.h"

// 前向声明
namespace cpporm_mysql_transport {
    class MySqlTransportStatement;
    class MySqlTransportResult;
}  // namespace cpporm_mysql_transport

namespace cpporm_sqldriver {
    class MySqlSpecificDriver;

    class MySqlSpecificResult : public SqlResult {
      public:
        explicit MySqlSpecificResult(const MySqlSpecificDriver* driver);
        ~MySqlSpecificResult() override;

        MySqlSpecificResult(const MySqlSpecificResult&) = delete;
        MySqlSpecificResult& operator=(const MySqlSpecificResult&) = delete;
        MySqlSpecificResult(MySqlSpecificResult&&) noexcept;
        MySqlSpecificResult& operator=(MySqlSpecificResult&&) noexcept;

        // --- SqlResult 接口实现 ---
        bool prepare(const std::string& query, const std::map<std::string, SqlValueType>* named_bindings_type_hints = nullptr, SqlResultNs::ScrollMode scroll = SqlResultNs::ScrollMode::ForwardOnly, SqlResultNs::ConcurrencyMode concur = SqlResultNs::ConcurrencyMode::ReadOnly) override;
        bool exec() override;
        bool setQueryTimeout(int seconds) override;
        bool setNumericalPrecisionPolicy(NumericalPrecisionPolicy policy) override;
        bool setPrefetchSize(int rows) override;
        int prefetchSize() const override;

        void addPositionalBindValue(const SqlValue& value, ParamType type = ParamType::In) override;
        void setNamedBindValue(const std::string& placeholder, const SqlValue& value, ParamType type = ParamType::In) override;
        void bindBlobStream(int pos, std::shared_ptr<std::istream> stream, long long size = -1, ParamType type = ParamType::In) override;
        void bindBlobStream(const std::string& placeholder, std::shared_ptr<std::istream> stream, long long size = -1, ParamType type = ParamType::In) override;
        void clearBindValues() override;
        void reset() override;
        bool setForwardOnly(bool forward) override;

        bool fetchNext(SqlRecord& record_buffer) override;
        bool fetchPrevious(SqlRecord& record_buffer) override;
        bool fetchFirst(SqlRecord& record_buffer) override;
        bool fetchLast(SqlRecord& record_buffer) override;
        bool fetch(int index, SqlRecord& record_buffer, CursorMovement movement = CursorMovement::Absolute) override;

        SqlValue data(int column_index) override;
        std::shared_ptr<std::istream> openReadableBlobStream(int column_index) override;
        std::shared_ptr<std::ostream> openWritableBlobStream(int column_index, long long initial_size_hint = 0) override;

        bool isNull(int column_index) override;
        SqlRecord recordMetadata() const override;
        SqlRecord currentFetchedRow() const override;
        SqlField field(int column_index) const override;

        long long numRowsAffected() override;
        SqlValue lastInsertId() override;
        int columnCount() const override;
        int size() override;
        int at() const override;

        bool isActive() const override;
        bool isValid() const override;
        SqlError error() const override;
        const std::string& lastQuery() const override;
        const std::string& preparedQueryText() const override;

        void finish() override;
        void clear() override;

        bool nextResult() override;

        SqlValue getOutParameter(int pos) const override;
        SqlValue getOutParameter(const std::string& name) const override;
        std::map<std::string, SqlValue> getAllOutParameters() const override;

        bool setNamedBindingSyntax(SqlResultNs::NamedBindingSyntax syntax) override;
        // --- End SqlResult 接口实现 ---

      private:
        const MySqlSpecificDriver* m_driver;
        std::unique_ptr<cpporm_mysql_transport::MySqlTransportStatement> m_transport_statement;
        std::unique_ptr<cpporm_mysql_transport::MySqlTransportResult> m_transport_result_set;

        std::string m_original_query_text;
        mysql_helper::NamedPlaceholderInfo m_placeholder_info;

        std::vector<SqlValue> m_positional_bind_values;
        std::map<std::string, SqlValue> m_named_bind_values_map;
        // 使用完整的命名空间
        std::vector<cpporm_mysql_transport::MySqlTransportBindParam> m_ordered_transport_bind_params;

        SqlRecord m_current_record_buffer_cache;
        long long m_current_row_index;
        my_ulonglong m_num_rows_affected_cache;  // my_ulonglong 来自 mysql.h
        SqlValue m_last_insert_id_cache;

        mutable SqlError m_last_error_cache;
        bool m_is_active_flag;
        NumericalPrecisionPolicy m_precision_policy;
        SqlResultNs::NamedBindingSyntax m_named_binding_syntax;
        SqlResultNs::ScrollMode m_scroll_mode_hint;
        int m_prefetch_size_hint;

        void updateLastErrorCacheFromTransportStatement();
        void updateLastErrorCacheFromTransportResult();
        void clearLastErrorCache();
        bool applyBindingsToTransportStatement();
        void cleanupAfterExecution(bool retain_result_set = false);
        bool ensureResultSet();
    };

}  // namespace cpporm_sqldriver