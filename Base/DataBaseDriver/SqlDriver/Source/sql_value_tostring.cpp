// SqlDriver/Source/sql_value_tostring.cpp
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <algorithm>  // For std::transform in boolean string conversion
#include <format>     // C++20/23/26 feature
#include <iomanip>
#include <limits>
#include <sstream>  // 仍然需要 ostringstream 用于浮点数，直到 std::format 对浮点数的支持完美且普遍
#include <string>
#include <variant>
#include <vector>

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {
    namespace detail {
        std::string blobToHexString(const std::vector<unsigned char>& blob);  // 声明来自 helpers
    }

    std::string SqlValue::toString(bool* ok, NumericalPrecisionPolicy /*policy*/) const {
        if (ok) *ok = false;
        if (isNull()) {
            if (ok) *ok = true;
            return "";
        }

        return std::visit(
            [ok, this](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    if (ok) *ok = true;
                    return "";
                } else if constexpr (std::is_same_v<T, bool>) {
                    if (ok) *ok = true;
                    return arg ? "true" : "false";
                } else if constexpr (std::is_integral_v<T>) {
                    if (ok) *ok = true;
                    return std::to_string(arg);
                }  // std::format("{}"...) 也可以
                else if constexpr (std::is_floating_point_v<T>) {
                    // std::format 对浮点数的默认精度可能与 std::ostringstream 不同，
                    // 如果需要特定精度，可能仍需 ostringstream 或显式格式化参数。
                    // C++23/26 的 std::format 对浮点数应该有很好的支持。
                    if (ok) *ok = true;
                    // 使用 std::format，如果默认精度不够，可以指定
                    // return std::format("{}", arg); // 默认精度
                    // 为了最大精度，ostringstream 仍然是一个选择，或者使用更精确的 std::format 参数
                    std::ostringstream oss;
                    oss << std::setprecision(std::numeric_limits<T>::max_digits10) << arg;
                    if (oss.fail()) {
                        return "";
                    }
                    return oss.str();
                } else if constexpr (std::is_same_v<T, std::string>) {
                    if (ok) *ok = true;
                    return arg;
                } else if constexpr (std::is_same_v<T, std::vector<unsigned char>>) {
                    if (ok) *ok = true;
                    return detail::blobToHexString(arg);
                } else if constexpr (std::is_same_v<T, QDate>) {
                    if (arg.isValid()) {
                        if (ok) *ok = true;
                        return arg.toString(Qt::ISODate).toStdString();
                    }
                } else if constexpr (std::is_same_v<T, QTime>) {
                    if (arg.isValid()) {
                        if (ok) *ok = true;
                        return arg.toString(Qt::ISODateWithMs).toStdString();
                    }
                } else if constexpr (std::is_same_v<T, QDateTime>) {
                    if (arg.isValid()) {
                        if (ok) *ok = true;
                        return arg.toString(Qt::ISODateWithMs).toStdString();
                    }
                } else if constexpr (std::is_same_v<T, ChronoDate>) {
                    if (arg.ok()) {
                        if (ok) *ok = true;
                        return std::format("{:%Y-%m-%d}", arg);
                    }
                } else if constexpr (std::is_same_v<T, ChronoTime>) {
                    if (ok) *ok = true;
                    // std::format 支持 std::chrono::duration
                    // 例如 "{:%H:%M:%S}" ，但可能需要 hh_mm_ss 适配器或手动格式化子秒
                    auto h = std::chrono::duration_cast<std::chrono::hours>(arg);
                    auto m = std::chrono::duration_cast<std::chrono::minutes>(arg % std::chrono::hours(1));
                    auto s = std::chrono::duration_cast<std::chrono::seconds>(arg % std::chrono::minutes(1));
                    auto us = std::chrono::duration_cast<std::chrono::microseconds>(arg % std::chrono::seconds(1));
                    std::string formatted_time = std::format("{:%T}", std::chrono::hh_mm_ss(arg).to_duration());  // %T is HH:MM:SS
                    if (us.count() != 0) {
                        std::string us_str = std::format("{:06}", us.count());
                        size_t last_digit = us_str.find_last_not_of('0');
                        if (last_digit != std::string::npos) {
                            formatted_time += "." + us_str.substr(0, last_digit + 1);
                        }
                    }
                    return formatted_time;
                } else if constexpr (std::is_same_v<T, ChronoDateTime>) {
                    if (ok) *ok = true;
                    // std::format 支持 system_clock::time_point，通常输出为 UTC ISO 8601
                    // 包含小数秒和 'Z'
                    return std::format("{:%Y-%m-%dT%H:%M:%S}Z", std::chrono::floor<std::chrono::seconds>(arg));  // 简化，去除小数秒以匹配之前的行为
                    // 要包含小数秒:
                    // return std::format("{0:%Y-%m-%dT%H:%M:%S}{1:%S}Z", arg, arg.time_since_epoch() % std::chrono::seconds(1)); // 这比较复杂
                    // 更简单的方法是针对 time_point 使用默认的 std::format，它通常做得很好
                    // return std::format("{}", arg); // C++26 应该对 time_point 有好的默认格式
                } else if constexpr (std::is_same_v<T, InputStreamPtr>) {
                    if (ok) *ok = true;
                    if (this->m_current_type_enum == SqlValueType::BinaryLargeObject) return "[BLOB StreamData]";
                    if (this->m_current_type_enum == SqlValueType::CharacterLargeObject) return "[CLOB StreamData]";
                    return "[InputStreamData]";
                } else if constexpr (std::is_same_v<T, std::any>) {
                    if (arg.type() == typeid(std::string)) {
                        if (ok) *ok = true;
                        return std::any_cast<std::string>(arg);
                    }
                    if (arg.type() == typeid(const char*)) {
                        if (ok) *ok = true;
                        return std::string(std::any_cast<const char*>(arg));
                    }
                    if (ok) *ok = true;
                    return "[CustomAnyData]";
                }
                return "";
            },
            m_value_storage);
    }

}  // namespace cpporm_sqldriver