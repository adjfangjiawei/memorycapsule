// (文件头部和其他部分不变)
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
        // (构造函数、析构函数、拷贝/移动控制不变)
        MySqlTransportStatement(MySqlTransportConnection* conn, const std::string& query);
        ~MySqlTransportStatement();

        MySqlTransportStatement(const MySqlTransportStatement&) = delete;
        MySqlTransportStatement& operator=(const MySqlTransportStatement&) = delete;
        MySqlTransportStatement(MySqlTransportStatement&& other) noexcept;
        MySqlTransportStatement& operator=(MySqlTransportStatement&& other) noexcept;

        // (prepare, isPrepared, bindParam, bindParams, execute, executeQuery,
        //  getAffectedRows, getLastInsertId, getWarningCount, getError, close,
        //  getNativeStatementHandle, getConnection 不变)
        bool prepare();
        bool isPrepared() const;

        bool bindParam(unsigned int pos, const MySqlTransportBindParam& param);
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
        // (clearError, setError, setErrorFromMySQL, setErrorFromProtocol 不变)
        void clearError();
        void setError(MySqlTransportError::Category cat, const std::string& msg, unsigned int proto_errc = 0);
        void setErrorFromMySQL();  // Gets error from m_stmt_handle or connection if stmt_handle is null
        void setErrorFromProtocol(const mysql_protocol::MySqlProtocolError& proto_err, const std::string& context);

        MySqlTransportConnection* m_connection;
        std::string m_original_query;
        MYSQL_STMT* m_stmt_handle;
        bool m_is_prepared;

        std::vector<MYSQL_BIND> m_bind_buffers;
        std::vector<std::vector<unsigned char>> m_param_data_buffers;  // Data storage for params
        std::vector<char> m_param_is_null_indicators;                  // Use char for my_bool (0 or 1)
        std::vector<unsigned long> m_param_length_indicators;
        // 如果支持输出参数，MYSQL_BIND 中的 is_null 指针也应指向 bool 类型数组
        // std::vector<bool> m_result_is_null_indicators; // 如果结果也用bind

        MySqlTransportError m_last_error;
        my_ulonglong m_affected_rows;
        my_ulonglong m_last_insert_id;
        unsigned int m_warning_count;
    };

}  // namespace cpporm_mysql_transport