// cpporm_mysql_transport/mysql_transport_result.h
#pragma once

#include <mysql/mysql.h>

#include <memory>
#include <string>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_types.h"
#include "mysql_protocol/mysql_type_converter.h"

namespace cpporm_mysql_transport {

    class MySqlTransportStatement;

    class MySqlTransportResult {
      public:
        // Constructor for PREPARED STATEMENTS (MYSQL_STMT*)
        MySqlTransportResult(MySqlTransportStatement* stmt, MYSQL_RES* meta_res_handle, MySqlTransportError& err_ref);
        // Constructor for NON-PREPARED STATEMENTS (MYSQL_RES* from mysql_store_result)
        MySqlTransportResult(MYSQL_RES* stored_res_handle, MySqlTransportError& err_ref);
        ~MySqlTransportResult();

        MySqlTransportResult(const MySqlTransportResult&) = delete;
        MySqlTransportResult& operator=(const MySqlTransportResult&) = delete;
        MySqlTransportResult(MySqlTransportResult&& other) noexcept;
        MySqlTransportResult& operator=(MySqlTransportResult&& other) noexcept;

        bool isValid() const;
        MySqlTransportError getError() const;
        my_ulonglong getRowCount() const;
        unsigned int getFieldCount() const;
        const std::vector<MySqlTransportFieldMeta>& getFieldsMeta() const;
        std::optional<MySqlTransportFieldMeta> getFieldMeta(unsigned int col_idx) const;
        std::optional<MySqlTransportFieldMeta> getFieldMeta(const std::string& col_name) const;
        int getFieldIndex(const std::string& col_name) const;

        bool fetchNextRow();
        std::optional<mysql_protocol::MySqlNativeValue> getValue(unsigned int col_idx);
        std::optional<mysql_protocol::MySqlNativeValue> getValue(const std::string& col_name);
        bool isNull(unsigned int col_idx);
        bool isNull(const std::string& col_name);
        std::vector<mysql_protocol::MySqlNativeValue> getCurrentRowValues();

        MYSQL_RES* getNativeMetadataHandle() const {
            return m_mysql_res_metadata;
        }  // For metadata
        MYSQL_STMT* getNativeStatementHandleForFetch() const {
            return m_mysql_stmt_handle_for_fetch;
        }  // For prepared stmt fetch

      private:
        void populateFieldsMeta();
        void clearCurrentRow();
        void setupOutputBindBuffers();  // Added declaration

        MySqlTransportStatement* m_statement;       // Null if from non-prepared MYSQL_RES
        MYSQL_RES* m_mysql_res_metadata;            // Metadata for prepared, or full result for non-prepared
        MYSQL_STMT* m_mysql_stmt_handle_for_fetch;  // Only for prepared statements for fetching
        MySqlTransportError& m_error_collector;     // Reference to error object (e.g., from statement or connection)

        std::vector<MySqlTransportFieldMeta> m_fields_meta;
        MYSQL_ROW m_current_sql_row;       // For non-prepared
        unsigned long* m_current_lengths;  // For non-prepared
        my_ulonglong m_row_count = 0;
        unsigned int m_field_count = 0;
        long long m_current_row_idx = -1;  // 0-based index of current fetched row, -1 if no row
        bool m_meta_populated = false;
        bool m_is_valid = false;
        bool m_is_from_prepared_statement = false;
        bool m_stmt_result_was_stored = false;  // For prepared: true if mysql_stmt_store_result succeeded

        // For prepared statement result binding
        std::vector<MYSQL_BIND> m_output_bind_buffers;
        std::vector<std::vector<unsigned char>> m_output_data_buffers;
        std::vector<char> m_output_is_null_indicators;  // char (0 or 1)
        std::vector<unsigned long> m_output_length_indicators;
        std::vector<char> m_output_error_indicators;  // char (0 or 1) for truncation/error
        bool m_fetched_all_from_stmt = false;         // For prepared: true if MYSQL_NO_DATA was returned
    };

}  // namespace cpporm_mysql_transport