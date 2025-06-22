// MySqlTransport/Include/cpporm_mysql_transport/mysql_transport_statement.h
#pragma once

#include <mysql/mysql.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mysql_transport_result.h"
#include "mysql_transport_types.h"

namespace cpporm_mysql_transport {

    class MySqlTransportConnection;

    class MySqlTransportStatement {
      public:
        MySqlTransportStatement(MySqlTransportConnection* conn, const std::string& query);
        ~MySqlTransportStatement();

        MySqlTransportStatement(const MySqlTransportStatement&) = delete;
        MySqlTransportStatement& operator=(const MySqlTransportStatement&) = delete;
        MySqlTransportStatement(MySqlTransportStatement&& other) noexcept;
        MySqlTransportStatement& operator=(MySqlTransportStatement&& other) noexcept;

        bool prepare();
        bool isPrepared() const;

        bool bindParam(unsigned int pos_zero_based, const MySqlTransportBindParam& param);
        bool bindParams(const std::vector<MySqlTransportBindParam>& params);

        std::optional<my_ulonglong> execute();
        std::unique_ptr<MySqlTransportResult> executeQuery();

        my_ulonglong getAffectedRows() const;
        my_ulonglong getLastInsertId() const;
        unsigned int getWarningCount() const;

        MySqlTransportError getError() const;

        void close();

        MYSQL_STMT* getNativeStatementHandle() const {
            return m_stmt_handle;
        }
        MySqlTransportConnection* getConnection() const {
            return m_connection;
        }

      private:
        void clearError();
        void setError(MySqlTransportError::Category cat, const std::string& msg, unsigned int proto_errc = 0);
        void setErrorFromMySQL();
        void setErrorFromProtocol(const mysql_protocol::MySqlProtocolError& proto_err, const std::string& context);

        MySqlTransportConnection* m_connection;
        std::string m_original_query;
        MYSQL_STMT* m_stmt_handle;
        bool m_is_prepared;

        std::vector<MYSQL_BIND> m_bind_buffers;
        std::vector<std::vector<unsigned char>> m_param_data_buffers;
        // ***** 关键修改: 使用 unsigned char 存储 NULL 指示符 (0 或 1) *****
        std::vector<unsigned char> m_param_is_null_indicators;
        std::vector<unsigned long> m_param_length_indicators;

        MySqlTransportError m_last_error;
        my_ulonglong m_affected_rows;
        my_ulonglong m_last_insert_id;
        unsigned int m_warning_count;
    };

}  // namespace cpporm_mysql_transport