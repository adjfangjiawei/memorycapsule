#ifndef cpporm_QUERY_BUILDER_CONDITIONS_MIXIN_H
#define cpporm_QUERY_BUILDER_CONDITIONS_MIXIN_H

#include "cpporm/builder_parts/query_builder_state.h"
#include "cpporm/error.h"
#include <expected>

#include <initializer_list>
#include <iterator>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#ifdef QT_CORE_LIB
#include <QDebug>
#endif

// Forward declare QueryBuilder for wrap_for_query_value and quoteSqlIdentifier
namespace cpporm {
class QueryBuilder;
}

namespace cpporm {

namespace detail {
template <typename T> struct is_std_initializer_list : std::false_type {};
template <typename E>
struct is_std_initializer_list<std::initializer_list<E>> : std::true_type {};
} // namespace detail

// Modified wrap_for_query_value
template <typename Arg>
QueryValue wrap_for_query_value(Arg &&arg) { // Explicitly return QueryValue
  using DecayedArg = std::decay_t<Arg>;
  if constexpr (std::is_same_v<DecayedArg, const char *> ||
                (std::is_array_v<DecayedArg> &&
                 std::is_same_v<std::remove_extent_t<DecayedArg>,
                                const char>)) {
    return std::string(std::forward<Arg>(arg));
  } else if constexpr (std::is_same_v<DecayedArg, SubqueryExpression>) {
    return std::forward<Arg>(arg);
  } else if constexpr (std::is_base_of_v<QueryBuilder, DecayedArg> ||
                       std::is_same_v<DecayedArg, QueryBuilder>) {
    // If 'arg' is a QueryBuilder (or derived from it), convert it to
    // SubqueryExpression. This requires QueryBuilder::AsSubquery() to be
    // accessible. We need to include "cpporm/query_builder_core.h" for
    // QueryBuilder::AsSubquery, or ensure QueryBuilder is fully defined. This
    // creates a dependency cycle risk if QueryBuilderCore includes this file.
    // A common pattern is to have QueryBuilder::AsSubquery() defined where
    // QueryBuilder is complete. For now, assume 'arg.AsSubquery()' is callable.
    auto sub_expr_expected = arg.AsSubquery();
    if (sub_expr_expected.has_value()) {
      return sub_expr_expected.value();
    } else {
#ifdef QT_CORE_LIB
      qWarning() << "wrap_for_query_value: Failed to convert QueryBuilder to "
                    "SubqueryExpression: "
                 << QString::fromStdString(sub_expr_expected.error().message)
                 << ". Returning nullptr for QueryValue.";
#endif
      return nullptr;
    }
  } else if constexpr (std::is_constructible_v<QueryValue, DecayedArg>) {
    return QueryValue(
        std::forward<Arg>(arg)); // Explicit construction for QueryValue
  } else {
    // Default forwarding for types directly convertible to one of QueryValue's
    // alternatives If this also fails, it will be a variant construction error.
    return std::forward<Arg>(arg);
  }
}

template <typename Derived> class QueryBuilderConditionsMixin {
protected:
  QueryBuilderState &_state() {
    return static_cast<Derived *>(this)->getState_();
  }
  const QueryBuilderState &_state() const {
    return static_cast<const Derived *>(this)->getState_();
  }

public:
  // --- WHERE methods ---
  Derived &Where(const std::string &query_string) {
    _state().where_conditions_.emplace_back(query_string);
    return static_cast<Derived &>(*this);
  }
  Derived &Where(const std::string &query_string,
                 const std::vector<QueryValue> &args) {
    _state().where_conditions_.emplace_back(query_string, args);
    return static_cast<Derived &>(*this);
  }
  Derived &Where(const std::string &query_string,
                 std::vector<QueryValue> &&args) {
    _state().where_conditions_.emplace_back(query_string, std::move(args));
    return static_cast<Derived &>(*this);
  }
  Derived &Where(const std::string &query_string,
                 std::initializer_list<QueryValue> il) {
    _state().where_conditions_.emplace_back(query_string, il);
    return static_cast<Derived &>(*this);
  }
  Derived &Where(const std::map<std::string, QueryValue> &conditions) {
    auto mc = mapToConditions(conditions);
    _state().where_conditions_.insert(_state().where_conditions_.end(),
                                      std::make_move_iterator(mc.begin()),
                                      std::make_move_iterator(mc.end()));
    return static_cast<Derived &>(*this);
  }

  Derived &
  Where(const std::string &query_string,
        const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
    if (sub_expr_expected.has_value()) {
      _state().where_conditions_.emplace_back(
          query_string, std::vector<QueryValue>{sub_expr_expected.value()});
    } else {
#ifdef QT_CORE_LIB
      qWarning() << "QueryBuilderConditionsMixin::Where(string, "
                    "expected<Subquery>): Subquery generation failed: "
                 << QString::fromStdString(sub_expr_expected.error().message)
                 << ". Condition based on this subquery will not be added.";
#endif
    }
    return static_cast<Derived &>(*this);
  }

  template <typename T, typename... TArgs,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<T>, std::vector<QueryValue>> &&
                    !std::is_same_v<std::decay_t<T>,
                                    std::map<std::string, QueryValue>> &&
                    !detail::is_std_initializer_list<std::decay_t<T>>::value &&
                    !std::is_same_v<std::decay_t<T>,
                                    std::expected<SubqueryExpression, Error>>,
                int> = 0>
  Derived &Where(const std::string &query_string, T &&val1,
                 TArgs &&...vals_rest) {
    std::vector<QueryValue> collected_args;
    collected_args.reserve(1 + sizeof...(TArgs));
    collected_args.push_back(
        wrap_for_query_value(std::forward<T>(val1))); // Changed to push_back
    if constexpr (sizeof...(TArgs) > 0) {
      (collected_args.push_back( // Changed to push_back
           wrap_for_query_value(std::forward<TArgs>(vals_rest))),
       ...);
    }
    _state().where_conditions_.emplace_back(query_string,
                                            std::move(collected_args));
    return static_cast<Derived &>(*this);
  }

  // --- OR methods --- (Similar structure to Where)
  Derived &Or(const std::string &query_string) {
    _state().or_conditions_.emplace_back(query_string);
    return static_cast<Derived &>(*this);
  }
  Derived &Or(const std::string &query_string,
              const std::vector<QueryValue> &args) {
    _state().or_conditions_.emplace_back(query_string, args);
    return static_cast<Derived &>(*this);
  }
  Derived &Or(const std::string &query_string, std::vector<QueryValue> &&args) {
    _state().or_conditions_.emplace_back(query_string, std::move(args));
    return static_cast<Derived &>(*this);
  }
  Derived &Or(const std::string &query_string,
              std::initializer_list<QueryValue> il) {
    _state().or_conditions_.emplace_back(query_string, il);
    return static_cast<Derived &>(*this);
  }
  Derived &Or(const std::map<std::string, QueryValue> &conditions) {
    auto mc = mapToConditions(conditions);
    _state().or_conditions_.insert(_state().or_conditions_.end(),
                                   std::make_move_iterator(mc.begin()),
                                   std::make_move_iterator(mc.end()));
    return static_cast<Derived &>(*this);
  }
  Derived &
  Or(const std::string &query_string,
     const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
    if (sub_expr_expected.has_value()) {
      _state().or_conditions_.emplace_back(
          query_string, std::vector<QueryValue>{sub_expr_expected.value()});
    } else {
#ifdef QT_CORE_LIB
      qWarning() << "QueryBuilderConditionsMixin::Or(string, "
                    "expected<Subquery>): Subquery generation failed: "
                 << QString::fromStdString(sub_expr_expected.error().message)
                 << ". Condition based on this subquery will not be added.";
#endif
    }
    return static_cast<Derived &>(*this);
  }

  template <typename T, typename... TArgs,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<T>, std::vector<QueryValue>> &&
                    !std::is_same_v<std::decay_t<T>,
                                    std::map<std::string, QueryValue>> &&
                    !detail::is_std_initializer_list<std::decay_t<T>>::value &&
                    !std::is_same_v<std::decay_t<T>,
                                    std::expected<SubqueryExpression, Error>>,
                int> = 0>
  Derived &Or(const std::string &query_string, T &&val1, TArgs &&...vals_rest) {
    std::vector<QueryValue> collected_args;
    collected_args.reserve(1 + sizeof...(TArgs));
    collected_args.push_back(
        wrap_for_query_value(std::forward<T>(val1))); // Changed
    if constexpr (sizeof...(TArgs) > 0) {
      (collected_args.push_back( // Changed
           wrap_for_query_value(std::forward<TArgs>(vals_rest))),
       ...);
    }
    _state().or_conditions_.emplace_back(query_string,
                                         std::move(collected_args));
    return static_cast<Derived &>(*this);
  }

  // --- NOT methods --- (Similar structure to Where)
  Derived &Not(const std::string &query_string) {
    _state().not_conditions_.emplace_back(query_string);
    return static_cast<Derived &>(*this);
  }
  Derived &Not(const std::string &query_string,
               const std::vector<QueryValue> &args) {
    _state().not_conditions_.emplace_back(query_string, args);
    return static_cast<Derived &>(*this);
  }
  Derived &Not(const std::string &query_string,
               std::vector<QueryValue> &&args) {
    _state().not_conditions_.emplace_back(query_string, std::move(args));
    return static_cast<Derived &>(*this);
  }
  Derived &Not(const std::string &query_string,
               std::initializer_list<QueryValue> il) {
    _state().not_conditions_.emplace_back(query_string, il);
    return static_cast<Derived &>(*this);
  }
  Derived &Not(const std::map<std::string, QueryValue> &conditions) {
    auto mc = mapToConditions(conditions);
    _state().not_conditions_.insert(_state().not_conditions_.end(),
                                    std::make_move_iterator(mc.begin()),
                                    std::make_move_iterator(mc.end()));
    return static_cast<Derived &>(*this);
  }
  Derived &
  Not(const std::string &query_string,
      const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
    if (sub_expr_expected.has_value()) {
      _state().not_conditions_.emplace_back(
          query_string, std::vector<QueryValue>{sub_expr_expected.value()});
    } else {
#ifdef QT_CORE_LIB
      qWarning() << "QueryBuilderConditionsMixin::Not(string, "
                    "expected<Subquery>): Subquery generation failed: "
                 << QString::fromStdString(sub_expr_expected.error().message)
                 << ". Condition based on this subquery will not be added.";
#endif
    }
    return static_cast<Derived &>(*this);
  }

  template <typename T, typename... TArgs,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<T>, std::vector<QueryValue>> &&
                    !std::is_same_v<std::decay_t<T>,
                                    std::map<std::string, QueryValue>> &&
                    !detail::is_std_initializer_list<std::decay_t<T>>::value &&
                    !std::is_same_v<std::decay_t<T>,
                                    std::expected<SubqueryExpression, Error>>,
                int> = 0>
  Derived &Not(const std::string &query_string, T &&val1,
               TArgs &&...vals_rest) {
    std::vector<QueryValue> collected_args;
    collected_args.reserve(1 + sizeof...(TArgs));
    collected_args.push_back(
        wrap_for_query_value(std::forward<T>(val1))); // Changed
    if constexpr (sizeof...(TArgs) > 0) {
      (collected_args.push_back( // Changed
           wrap_for_query_value(std::forward<TArgs>(vals_rest))),
       ...);
    }
    _state().not_conditions_.emplace_back(query_string,
                                          std::move(collected_args));
    return static_cast<Derived &>(*this);
  }

  // --- IN methods ---
  // Takes a vector of QueryValue directly
  Derived &In(const std::string &column_name,
              const std::vector<QueryValue> &values) {
    if (values.empty()) {
      // For an empty IN list, "1 = 0" ensures no rows are matched.
      _state().where_conditions_.emplace_back("1 = 0");
      return static_cast<Derived &>(*this);
    }

    std::string placeholders;
    placeholders.reserve(values.size() * 3); // Approximate size for "?, ?, ?"
    for (size_t i = 0; i < values.size(); ++i) {
      placeholders += (i == 0 ? "?" : ", ?");
    }

    // QueryBuilder::quoteSqlIdentifier is a public static method
    // This creates a circular dependency if QueryBuilder needs this header.
    // For now, assuming QueryBuilder is defined before this, or
    // quoteSqlIdentifier is moved/forwarded. Let's assume
    // `static_cast<Derived*>(this)->quoteSqlIdentifier()` exists or a global
    // one. For simplicity, we will call a static method assumed to be
    // available.
    // **This means QueryBuilder must be fully defined before this header, or a
    // static helper for quoting must be in a common place.** The easiest is
    // that QueryBuilder.h includes this.
    std::string quoted_column =
        static_cast<Derived *>(this)->quoteSqlIdentifier(
            column_name); // Calling through Derived
    _state().where_conditions_.emplace_back(
        quoted_column + " IN (" + placeholders + ")", values);
    return static_cast<Derived &>(*this);
  }

  Derived &In(const std::string &column_name,
              std::vector<QueryValue> &&values) {
    if (values.empty()) {
      _state().where_conditions_.emplace_back("1 = 0");
      return static_cast<Derived &>(*this);
    }
    std::string placeholders;
    placeholders.reserve(values.size() * 3);
    for (size_t i = 0; i < values.size(); ++i) {
      placeholders += (i == 0 ? "?" : ", ?");
    }
    std::string quoted_column =
        static_cast<Derived *>(this)->quoteSqlIdentifier(column_name);
    _state().where_conditions_.emplace_back(
        quoted_column + " IN (" + placeholders + ")", std::move(values));
    return static_cast<Derived &>(*this);
  }

  // Templated version for convenience, taking a vector of arbitrary types
  template <typename T>
  Derived &In(const std::string &column_name, const std::vector<T> &values) {
    if (values.empty()) {
      _state().where_conditions_.emplace_back("1 = 0");
      return static_cast<Derived &>(*this);
    }
    std::vector<QueryValue> qv_values;
    qv_values.reserve(values.size());
    for (const auto &val : values) {
      qv_values.push_back(
          wrap_for_query_value(val)); // Use existing wrap_for_query_value
    }
    return In(column_name,
              std::move(qv_values)); // Delegate to QueryValue vector overload
  }

  // Overload for initializer_list<QueryValue>
  Derived &In(const std::string &column_name,
              std::initializer_list<QueryValue> il) {
    // Create a vector from the initializer_list to reuse the vector overload
    return In(column_name, std::vector<QueryValue>(il.begin(), il.end()));
  }

  // Templated version for initializer_list<T> where T is not QueryValue
  template <
      typename T,
      std::enable_if_t<!std::is_same_v<std::decay_t<T>, QueryValue> &&
                           !std::is_same_v<std::decay_t<T>, SubqueryExpression>,
                       int> = 0>
  Derived &In(const std::string &column_name, std::initializer_list<T> il) {
    if (il.size() == 0) {
      _state().where_conditions_.emplace_back("1 = 0");
      return static_cast<Derived &>(*this);
    }
    std::vector<QueryValue> qv_values;
    qv_values.reserve(il.size());
    for (const T &val : il) {
      qv_values.push_back(wrap_for_query_value(val));
    }
    return In(column_name, std::move(qv_values)); // Delegate
  }

  const std::vector<Condition> &getWhereConditions_mixin() const {
    return _state().where_conditions_;
  }
  const std::vector<Condition> &getOrConditions_mixin() const {
    return _state().or_conditions_;
  }
  const std::vector<Condition> &getNotConditions_mixin() const {
    return _state().not_conditions_;
  }
};

} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_CONDITIONS_MIXIN_H