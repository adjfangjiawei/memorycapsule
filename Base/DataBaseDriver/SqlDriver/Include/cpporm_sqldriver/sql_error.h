// cpporm_sqldriver/sql_error.h
#pragma once
#include <string>

namespace cpporm_sqldriver {

    // ErrorType 可以更细化，模仿 QSqlError::ErrorType
    enum class ErrorType {
        NoError = 0,
        ConnectionError,   // 连接相关错误
        StatementError,    // SQL语句执行错误 (语法、权限等)
        TransactionError,  // 事务操作错误
        UnknownError       // 其他或未知错误
    };

    class SqlError {
      public:
        SqlError();  // NoError
        SqlError(ErrorType type,
                 const std::string& databaseText,          // 数据库返回的错误信息
                 const std::string& driverText = "",       // 驱动层产生的错误信息
                 const std::string& nativeErrorCode = "",  // 数据库的原生错误码 (字符串形式)
                 int nativeDbCodeNumeric = 0);             // 数据库的原生错误码 (数字形式, 如果有)

        ErrorType type() const;
        std::string databaseText() const;
        std::string driverText() const;
        std::string text() const;             // 综合性错误信息
        std::string nativeErrorCode() const;  // 字符串形式的原生错误码
        int nativeErrorCodeNumeric() const;   // 数字形式的原生错误码 (新增)
        bool isValid() const;                 // type() != NoError

        void setType(ErrorType type);
        void setDatabaseText(const std::string& text);
        void setDriverText(const std::string& text);
        void setNativeErrorCode(const std::string& code);
        void setNativeErrorCodeNumeric(int code);

      private:
        ErrorType type_ = ErrorType::NoError;
        std::string database_text_;
        std::string driver_text_;
        std::string native_error_code_str_;
        int native_error_code_num_ = 0;
    };

}  // namespace cpporm_sqldriver