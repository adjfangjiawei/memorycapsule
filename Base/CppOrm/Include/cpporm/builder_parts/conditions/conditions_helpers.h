#ifndef cpporm_QUERY_BUILDER_CONDITIONS_HELPERS_H
#define cpporm_QUERY_BUILDER_CONDITIONS_HELPERS_H

#include <expected>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

#include "cpporm/builder_parts/query_builder_state.h"
#include "cpporm/error.h"

#ifdef QT_CORE_LIB
#include <QDebug>
#endif

// Forward declare QueryBuilder for wrap_for_query_value
namespace cpporm {
    class QueryBuilder;
}

namespace cpporm {

    namespace detail {
        template <typename T>
        struct is_std_initializer_list : std::false_type {};
        template <typename E>
        struct is_std_initializer_list<std::initializer_list<E>> : std::true_type {};
    }  // namespace detail

    template <typename Arg>
    QueryValue wrap_for_query_value(Arg &&arg) {  // Explicitly return QueryValue
        using DecayedArg = std::decay_t<Arg>;
        if constexpr (std::is_same_v<DecayedArg, const char *> || (std::is_array_v<DecayedArg> && std::is_same_v<std::remove_extent_t<DecayedArg>, const char>)) {
            return std::string(std::forward<Arg>(arg));
        } else if constexpr (std::is_same_v<DecayedArg, SubqueryExpression>) {
            return std::forward<Arg>(arg);
        } else if constexpr (std::is_base_of_v<QueryBuilder, DecayedArg> || std::is_same_v<DecayedArg, QueryBuilder>) {
            auto sub_expr_expected = arg.AsSubquery();
            if (sub_expr_expected.has_value()) {
                return sub_expr_expected.value();
            } else {
#ifdef QT_CORE_LIB
                qWarning() << "wrap_for_query_value: Failed to convert QueryBuilder to "
                              "SubqueryExpression: "
                           << QString::fromStdString(sub_expr_expected.error().message) << ". Returning nullptr for QueryValue.";
#endif
                return nullptr;
            }
            // ***** 新增分支来处理 enum class *****
        } else if constexpr (std::is_enum_v<DecayedArg>) {
            // 将 enum class 转换为其底层整数类型
            return static_cast<std::underlying_type_t<DecayedArg>>(std::forward<Arg>(arg));
        } else if constexpr (std::is_constructible_v<QueryValue, DecayedArg>) {
            return QueryValue(std::forward<Arg>(arg));  // Explicit construction for QueryValue
        } else {
            return std::forward<Arg>(arg);
        }
    }

}  // namespace cpporm

#endif  // cpporm_QUERY_BUILDER_CONDITIONS_HELPERS_H