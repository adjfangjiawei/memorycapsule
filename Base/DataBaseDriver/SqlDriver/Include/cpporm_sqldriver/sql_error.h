// cpporm_sqldriver/sql_error.h
#pragma once
#include <optional>
#include <string>

namespace cpporm_sqldriver {

    // ErrorCategory 和 ErrorType 枚举定义在文件顶部或类的外部
    enum class ErrorCategory { NoError, Connectivity, Syntax, Constraint, Permissions, DataRelated, Resource, Transaction, DriverInternal, DatabaseInternal, OperationCancelled, FeatureNotSupported, Unknown };

    // 旧的 ErrorType，如果仍需保留用于映射或兼容
    enum class ErrorType {
        NoError = 0,  // 保持与Qt QSqlError::NoError 一致
        ConnectionError,
        StatementError,
        TransactionError,
        UnknownError,
        FeatureNotSupportedError,
        DataError,
        ConstraintViolationError
    };

    class SqlError {
      public:
        SqlError();
        SqlError(ErrorCategory category,
                 const std::string& databaseText,
                 const std::string& driverText = "",
                 const std::string& nativeErrorCode = "",
                 int nativeDbCodeNumeric = 0,
                 const std::string& failedQuery = "",
                 const std::string& constraintName = "",
                 const std::optional<int>& errorOffset = std::nullopt);

        ErrorCategory category() const;
        ErrorType type() const;  // 可以基于 category() 返回一个映射的 ErrorType
        std::string databaseText() const;
        std::string driverText() const;
        std::string text() const;
        std::string nativeErrorCode() const;
        int nativeErrorCodeNumeric() const;
        std::string failedQuery() const;
        std::string constraintName() const;
        std::optional<int> errorOffsetInQuery() const;
        bool isValid() const;  // category() != ErrorCategory::NoError

        void setCategory(ErrorCategory category);
        void setType(ErrorType type);
        void setDatabaseText(const std::string& text);
        void setDriverText(const std::string& text);
        void setNativeErrorCode(const std::string& code);
        void setNativeErrorCodeNumeric(int code);
        void setFailedQuery(const std::string& query);
        void setConstraintName(const std::string& name);
        void setErrorOffsetInQuery(const std::optional<int>& offset);
        void clear();

      private:
        ErrorCategory category_ = ErrorCategory::NoError;
        ErrorType legacy_type_ = ErrorType::NoError;
        std::string database_text_;
        std::string driver_text_;
        std::string native_error_code_str_;
        int native_error_code_num_ = 0;
        std::string failed_query_;
        std::string constraint_name_;
        std::optional<int> error_offset_;
    };

}  // namespace cpporm_sqldriver