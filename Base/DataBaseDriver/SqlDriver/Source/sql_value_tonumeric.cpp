// SqlDriver/Source/sql_value_tonumeric.cpp
#include <algorithm>  // For std::transform (tolower)
#include <charconv>   // For std::from_chars
#include <limits>     // For std::numeric_limits
#include <string>     // For std::string
#include <variant>    // For std::visit

#include "sqldriver/sql_value.h"

// 辅助函数定义 (如果它们不放在 sql_value_helpers.cpp)
namespace cpporm_sqldriver {
    namespace detail {

        template <typename IntType>
        std::optional<IntType> stringToInteger(const std::string& s, bool* ok);  // 声明 (实现在 helpers 或此处)

        template <typename FloatType>
        std::optional<FloatType> stringToFloat(const std::string& s, bool* ok);  // 声明

// 如果 stringToInteger/Float 的定义不在此处，则需要包含 sql_value_helpers.cpp 或其头文件
// 为了独立编译，这里提供一个最小实现
#ifndef SQLVALUE_HELPERS_DEFINED  // 防止重复定义（如果 helpers 文件也包含它们）
#define SQLVALUE_HELPERS_DEFINED
        template <typename IntType>
        std::optional<IntType> stringToInteger(const std::string& s, bool* ok) {
            if (s.empty()) {
                if (ok) *ok = false;
                return std::nullopt;
            }
            IntType val{};
            size_t first = s.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string::npos) {
                if (ok) *ok = false;
                return std::nullopt;
            }
            size_t last = s.find_last_not_of(" \t\n\r\f\v");
            std::string_view sv_trimmed = std::string_view(s).substr(first, last - first + 1);
            auto [ptr, ec] = std::from_chars(sv_trimmed.data(), sv_trimmed.data() + sv_trimmed.size(), val);
            if (ec == std::errc() && ptr == sv_trimmed.data() + sv_trimmed.size()) {
                if (ok) *ok = true;
                return val;
            }
            if (ok) *ok = false;
            return std::nullopt;
        }

        template <typename FloatType>
        std::optional<FloatType> stringToFloat(const std::string& s, bool* ok) {
            if (s.empty()) {
                if (ok) *ok = false;
                return std::nullopt;
            }
            size_t first = s.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string::npos) {
                if (ok) *ok = false;
                return std::nullopt;
            }
            size_t last = s.find_last_not_of(" \t\n\r\f\v");
            std::string s_trimmed = s.substr(first, last - first + 1);
            try {
                size_t idx = 0;
                FloatType val{};
                if constexpr (std::is_same_v<FloatType, float>)
                    val = std::stof(s_trimmed, &idx);
                else if constexpr (std::is_same_v<FloatType, double>)
                    val = std::stod(s_trimmed, &idx);
                else if constexpr (std::is_same_v<FloatType, long double>)
                    val = std::stold(s_trimmed, &idx);
                if (idx == s_trimmed.length()) {
                    if (ok) *ok = true;
                    return val;
                }
            } catch (...) {
            }
            if (ok) *ok = false;
            return std::nullopt;
        }
#endif  // SQLVALUE_HELPERS_DEFINED
    }  // namespace detail

    bool SqlValue::toBool(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return false;

        return std::visit(
            [ok](auto&& arg) -> bool {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, bool>) {
                    if (ok) *ok = true;
                    return arg;
                } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
                    if (ok) *ok = true;
                    return arg != 0;
                } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<T, long double>) {
                    if (ok) *ok = true;
                    return arg != static_cast<T>(0.0);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    std::string s_lower = arg;
                    std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    if (s_lower == "true" || s_lower == "1" || s_lower == "t" || s_lower == "yes" || s_lower == "on") {
                        if (ok) *ok = true;
                        return true;
                    }
                    if (s_lower == "false" || s_lower == "0" || s_lower == "f" || s_lower == "no" || s_lower == "off") {
                        if (ok) *ok = true;
                        return false;
                    }
                }
                return false;
            },
            m_value_storage);
    }

// 使用之前定义的宏，现在它包含了更完整的转换逻辑
#define SQLVALUE_TO_NUMERIC_IMPL_DEF(ReturnType, VariantTypeOrBase, MethodName, DefaultValIfNull)                                                                                                                                            \
    ReturnType SqlValue::MethodName(bool* ok, NumericalPrecisionPolicy policy) const {                                                                                                                                                       \
        if (ok) *ok = false;                                                                                                                                                                                                                 \
        if (isNull()) return DefaultValIfNull;                                                                                                                                                                                               \
        /* 1. 尝试直接获取 (如果内部存储的就是目标类型或其基础类型) */                                                                                                                                                                       \
        if constexpr (std::is_floating_point_v<ReturnType>) { /* 目标是浮点数 */                                                                                                                                                             \
            if (std::holds_alternative<ReturnType>(m_value_storage)) {                                                                                                                                                                       \
                if (ok) *ok = true;                                                                                                                                                                                                          \
                return std::get<ReturnType>(m_value_storage);                                                                                                                                                                                \
            } else if (std::holds_alternative<float>(m_value_storage) && !std::is_same_v<ReturnType, float>) {                                                                                                                               \
                if (ok) *ok = true;                                                                                                                                                                                                          \
                return static_cast<ReturnType>(std::get<float>(m_value_storage));                                                                                                                                                            \
            } else if (std::holds_alternative<double>(m_value_storage) && !std::is_same_v<ReturnType, double>) {                                                                                                                             \
                if (ok) *ok = true;                                                                                                                                                                                                          \
                return static_cast<ReturnType>(std::get<double>(m_value_storage));                                                                                                                                                           \
            } else if (std::holds_alternative<long double>(m_value_storage) && !std::is_same_v<ReturnType, long double>) {                                                                                                                   \
                if (ok) *ok = true;                                                                                                                                                                                                          \
                return static_cast<ReturnType>(std::get<long double>(m_value_storage));                                                                                                                                                      \
            }                                                                                                                                                                                                                                \
        } else if constexpr (std::is_integral_v<ReturnType>) { /* 目标是整数 */                                                                                                                                                              \
            if (std::holds_alternative<ReturnType>(m_value_storage)) {                                                                                                                                                                       \
                if (ok) *ok = true;                                                                                                                                                                                                          \
                return std::get<ReturnType>(m_value_storage);                                                                                                                                                                                \
            }                                                                                                                                                                                                                                \
        }                                                                                                                                                                                                                                    \
        /* 2. 尝试从其他数字类型转换 */                                                                                                                                                                                                      \
        return std::visit(                                                                                                                                                                                                                   \
            [ok, policy](auto&& arg) -> ReturnType {                                                                                                                                                                                         \
                using SrcT = std::decay_t<decltype(arg)>;                                                                                                                                                                                    \
                if constexpr (std::is_arithmetic_v<SrcT> && !std::is_same_v<SrcT, bool>) {                                                                                                                                                   \
                    if constexpr (std::is_floating_point_v<ReturnType>) { /* int to float/double/ldouble, float to double/ldouble, double to ldouble */                                                                                      \
                                                                          /* 检查范围对于整数转浮点数通常不是问题，除非是非常大的整数 */                                                                                                     \
                        if constexpr (std::is_integral_v<SrcT>) {                                                                                                                                                                            \
                            if (static_cast<long double>(arg) >= static_cast<long double>(std::numeric_limits<ReturnType>::lowest()) && static_cast<long double>(arg) <= static_cast<long double>(std::numeric_limits<ReturnType>::max())) { \
                                if (ok) *ok = true;                                                                                                                                                                                          \
                                return static_cast<ReturnType>(arg);                                                                                                                                                                         \
                            }                                                                                                                                                                                                                \
                        } else { /* float to float/double/ldouble */                                                                                                                                                                         \
                            if (ok) *ok = true;                                                                                                                                                                                              \
                            return static_cast<ReturnType>(arg);                                                                                                                                                                             \
                        }                                                                                                                                                                                                                    \
                    } else if constexpr (std::is_integral_v<ReturnType>) {                 /* float/double/ldouble to int, or int to int */                                                                                                  \
                        if constexpr (std::is_floating_point_v<SrcT>) {                    /* float/double to int */                                                                                                                         \
                            if (policy != NumericalPrecisionPolicy::ExactRepresentation) { /* 允许截断 */                                                                                                                                    \
                                if (arg >= static_cast<SrcT>(std::numeric_limits<ReturnType>::lowest()) && arg <= static_cast<SrcT>(std::numeric_limits<ReturnType>::max())) {                                                               \
                                    if (ok) *ok = true;                                                                                                                                                                                      \
                                    return static_cast<ReturnType>(arg);                                                                                                                                                                     \
                                }                                                                                                                                                                                                            \
                            }                                                                                                                                                                                                                \
                        } else { /* int to int */                                                                                                                                                                                            \
                            if (arg >= static_cast<long long>(std::numeric_limits<ReturnType>::min()) && arg <= static_cast<long long>(std::numeric_limits<ReturnType>::max())) {                                                            \
                                if (ok) *ok = true;                                                                                                                                                                                          \
                                return static_cast<ReturnType>(arg);                                                                                                                                                                         \
                            }                                                                                                                                                                                                                \
                        }                                                                                                                                                                                                                    \
                    }                                                                                                                                                                                                                        \
                } else if constexpr (std::is_same_v<SrcT, std::string>) {                                                                                                                                                                    \
                    if (policy != NumericalPrecisionPolicy::ExactRepresentation) {                                                                                                                                                           \
                        if constexpr (std::is_integral_v<ReturnType>) {                                                                                                                                                                      \
                            auto converted = detail::stringToInteger<ReturnType>(arg, ok);                                                                                                                                                   \
                            if (converted) return *converted;                                                                                                                                                                                \
                        } else if constexpr (std::is_floating_point_v<ReturnType>) {                                                                                                                                                         \
                            auto converted = detail::stringToFloat<ReturnType>(arg, ok);                                                                                                                                                     \
                            if (converted) return *converted;                                                                                                                                                                                \
                        }                                                                                                                                                                                                                    \
                    }                                                                                                                                                                                                                        \
                } else if constexpr (std::is_same_v<SrcT, bool> && std::is_arithmetic_v<ReturnType>) {                                                                                                                                       \
                    if (ok) *ok = true;                                                                                                                                                                                                      \
                    return static_cast<ReturnType>(arg ? 1 : 0);                                                                                                                                                                             \
                }                                                                                                                                                                                                                            \
                return DefaultValIfNull;                                                                                                                                                                                                     \
            },                                                                                                                                                                                                                               \
            m_value_storage);                                                                                                                                                                                                                \
    }

    SQLVALUE_TO_NUMERIC_IMPL_DEF(int8_t, int8_t, toInt8, 0)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(uint8_t, uint8_t, toUInt8, 0)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(int16_t, int16_t, toInt16, 0)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(uint16_t, uint16_t, toUInt16, 0)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(int32_t, int32_t, toInt32, 0)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(uint32_t, uint32_t, toUInt32, 0)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(int64_t, int64_t, toInt64, 0LL)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(uint64_t, uint64_t, toUInt64, 0ULL)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(float, float, toFloat, 0.0f)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(double, double, toDouble, 0.0)
    SQLVALUE_TO_NUMERIC_IMPL_DEF(long double, long double, toLongDouble, 0.0L)

}  // namespace cpporm_sqldriver