#ifndef cpporm_ERROR_H
#define cpporm_ERROR_H

#include <string>
#include <system_error> // For std::error_code, std::errc (potential future use)

namespace cpporm {

// 错误码枚举
enum class ErrorCode {
  Ok = 0,
  // 连接相关错误
  ConnectionFailed,
  ConnectionAlreadyOpen,
  ConnectionNotOpen,
  ConnectionInvalid,
  DriverNotFound,
  // 配置错误
  InvalidConfiguration,
  // SQL 执行错误
  QueryExecutionError,
  StatementPreparationError,
  TransactionError,
  // ORM 层面错误
  RecordNotFound,
  MappingError,
  UnsupportedFeature,
  // 其他
  InternalError,
  UnknownError,
};

// Error 结构体，用于封装错误信息
struct Error {
  ErrorCode code = ErrorCode::Ok;
  std::string message;
  int native_db_error_code = 0; // 可选的数据库原生错误码
  std::string sql_state;        // 可选的 SQLSTATE

  // 构造函数
  Error() = default;
  Error(ErrorCode c, std::string msg = "", int native_code = 0,
        std::string state = "")
      : code(c), message(std::move(msg)), native_db_error_code(native_code),
        sql_state(std::move(state)) {}

  // 检查是否为成功状态
  bool isOk() const { return code == ErrorCode::Ok; }

  // 允许在布尔上下文中使用 (if (error))
  explicit operator bool() const {
    return !isOk(); // true if there is an error
  }

  // 获取错误描述
  std::string toString() const {
    std::string err_str =
        "Error Code: " + std::to_string(static_cast<int>(code));
    if (!message.empty()) {
      err_str += ", Message: " + message;
    }
    if (native_db_error_code != 0) {
      err_str += ", DB Error: " + std::to_string(native_db_error_code);
    }
    if (!sql_state.empty()) {
      err_str += ", SQLState: " + sql_state;
    }
    return err_str;
  }
};

// 一个辅助函数，用于快速创建 Ok 状态的 Error
inline Error make_ok() { return Error(ErrorCode::Ok); }

} // namespace cpporm

#endif // cpporm_ERROR_H