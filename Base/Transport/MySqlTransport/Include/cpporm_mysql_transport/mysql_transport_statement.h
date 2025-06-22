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

        std::optional<my_ulonglong> execute();                 // For DML
        std::unique_ptr<MySqlTransportResult> executeQuery();  // For SELECT and utility commands like SHOW

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
        bool isUtilityCommand() const {
            return m_is_utility_command;
        }

      private:
        void clearError();
        void setError(MySqlTransportError::Category cat, const std::string& msg, unsigned int proto_errc = 0);
        void setErrorFromMySQL(MYSQL* handle_to_check_error_on, const std::string& context);
        void setErrorFromProtocol(const mysql_protocol::MySqlProtocolError& proto_err, const std::string& context);

        MySqlTransportConnection* m_connection;
        std::string m_original_query;
        MYSQL_STMT* m_stmt_handle;  // Will be nullptr for utility commands
        bool m_is_prepared;
        bool m_is_utility_command;

        std::vector<MYSQL_BIND> m_bind_buffers;
        std::vector<std::vector<unsigned char>> m_param_data_buffers;
        std::vector<unsigned char> m_param_is_null_indicators;
        std::vector<unsigned long> m_param_length_indicators;

        MySqlTransportError m_last_error;
        my_ulonglong m_affected_rows;
        my_ulonglong m_last_insert_id;
        unsigned int m_warning_count;
    };

}  // namespace cpporm_mysql_transport