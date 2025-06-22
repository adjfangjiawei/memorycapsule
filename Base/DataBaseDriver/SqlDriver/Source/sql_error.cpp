// SqlDriver/Source/sql_error.cpp
#include "sqldriver/sql_error.h"

#include <string>  // For std::to_string

namespace cpporm_sqldriver {

    SqlError::SqlError() : category_(ErrorCategory::NoError), legacy_type_(ErrorType::NoError), native_error_code_num_(0) {
    }

    SqlError::SqlError(ErrorCategory category, const std::string& databaseText, const std::string& driverText, const std::string& nativeErrorCodeStr, int nativeDbCodeNumeric, const std::string& failedQuery, const std::string& constraintName, const std::optional<int>& errorOffset)
        : category_(category), database_text_(databaseText), driver_text_(driverText), native_error_code_str_(nativeErrorCodeStr), native_error_code_num_(nativeDbCodeNumeric), failed_query_(failedQuery), constraint_name_(constraintName), error_offset_(errorOffset) {
        // Automatically set legacy_type_ based on category_ for basic compatibility
        switch (category_) {
            case ErrorCategory::NoError:
                legacy_type_ = ErrorType::NoError;
                break;
            case ErrorCategory::Connectivity:
                legacy_type_ = ErrorType::ConnectionError;
                break;
            case ErrorCategory::Syntax:
                legacy_type_ = ErrorType::StatementError;
                break;  // Or UnknownError
            case ErrorCategory::Constraint:
                legacy_type_ = ErrorType::ConstraintViolationError;
                break;  // Or StatementError
            case ErrorCategory::Permissions:
                legacy_type_ = ErrorType::ConnectionError;
                break;  // Or StatementError
            case ErrorCategory::DataRelated:
                legacy_type_ = ErrorType::DataError;
                break;
            case ErrorCategory::Resource:
                legacy_type_ = ErrorType::UnknownError;
                break;  // Or StatementError
            case ErrorCategory::Transaction:
                legacy_type_ = ErrorType::TransactionError;
                break;
            case ErrorCategory::DriverInternal:
                legacy_type_ = ErrorType::UnknownError;
                break;
            case ErrorCategory::DatabaseInternal:
                legacy_type_ = ErrorType::UnknownError;
                break;
            case ErrorCategory::OperationCancelled:
                legacy_type_ = ErrorType::UnknownError;
                break;
            case ErrorCategory::FeatureNotSupported:
                legacy_type_ = ErrorType::FeatureNotSupportedError;
                break;
            default:
                legacy_type_ = ErrorType::UnknownError;
                break;
        }
    }

    ErrorCategory SqlError::category() const {
        return category_;
    }

    ErrorType SqlError::type() const {
        // Return the legacy type. Could also dynamically map from category_ if preferred.
        return legacy_type_;
    }

    std::string SqlError::databaseText() const {
        return database_text_;
    }

    std::string SqlError::driverText() const {
        return driver_text_;
    }

    std::string SqlError::text() const {
        // Combine driver and database text for a comprehensive message
        if (!driver_text_.empty() && !database_text_.empty()) {
            if (driver_text_ == database_text_) return driver_text_;
            return driver_text_ + " (Database: " + database_text_ + ")";
        }
        if (!driver_text_.empty()) {
            return driver_text_;
        }
        return database_text_;
    }

    std::string SqlError::nativeErrorCode() const {
        return native_error_code_str_;
    }

    int SqlError::nativeErrorCodeNumeric() const {
        return native_error_code_num_;
    }

    std::string SqlError::failedQuery() const {
        return failed_query_;
    }
    std::string SqlError::constraintName() const {
        return constraint_name_;
    }
    std::optional<int> SqlError::errorOffsetInQuery() const {
        return error_offset_;
    }

    bool SqlError::isValid() const {
        return category_ != ErrorCategory::NoError;
    }

    void SqlError::setCategory(ErrorCategory category) {
        category_ = category;
        // Optionally re-map legacy_type_ here if needed
    }

    void SqlError::setType(ErrorType type) {
        legacy_type_ = type;
        // Optionally, try to map back to a category_ if this is the primary setter
    }

    void SqlError::setDatabaseText(const std::string& text) {
        database_text_ = text;
    }

    void SqlError::setDriverText(const std::string& text) {
        driver_text_ = text;
    }

    void SqlError::setNativeErrorCode(const std::string& code) {
        native_error_code_str_ = code;
    }

    void SqlError::setNativeErrorCodeNumeric(int code) {
        native_error_code_num_ = code;
    }

    void SqlError::setFailedQuery(const std::string& query) {
        failed_query_ = query;
    }
    void SqlError::setConstraintName(const std::string& name) {
        constraint_name_ = name;
    }
    void SqlError::setErrorOffsetInQuery(const std::optional<int>& offset) {
        error_offset_ = offset;
    }

    void SqlError::clear() {
        category_ = ErrorCategory::NoError;
        legacy_type_ = ErrorType::NoError;
        database_text_.clear();
        driver_text_.clear();
        native_error_code_str_.clear();
        native_error_code_num_ = 0;
        failed_query_.clear();
        constraint_name_.clear();
        error_offset_ = std::nullopt;
    }

}  // namespace cpporm_sqldriver