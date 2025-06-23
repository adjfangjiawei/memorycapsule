#ifndef cpporm_QUERY_BUILDER_OR_MIXIN_H
#define cpporm_QUERY_BUILDER_OR_MIXIN_H

#include <map>

#include "cpporm/builder_parts/conditions/conditions_helpers.h"

namespace cpporm {

    template <typename Derived>
    class OrMixin {
      protected:
        QueryBuilderState &_state() {
            return static_cast<Derived *>(this)->getState_();
        }

      public:
        Derived &Or(const std::string &query_string) {
            _state().or_conditions_.emplace_back(query_string);
            return static_cast<Derived &>(*this);
        }

        Derived &Or(const std::string &query_string, const std::vector<QueryValue> &args) {
            _state().or_conditions_.emplace_back(query_string, args);
            return static_cast<Derived &>(*this);
        }

        Derived &Or(const std::string &query_string, std::vector<QueryValue> &&args) {
            _state().or_conditions_.emplace_back(query_string, std::move(args));
            return static_cast<Derived &>(*this);
        }

        Derived &Or(const std::string &query_string, std::initializer_list<QueryValue> il) {
            _state().or_conditions_.emplace_back(query_string, il);
            return static_cast<Derived &>(*this);
        }

        Derived &Or(const std::map<std::string, QueryValue> &conditions) {
            auto mc = mapToConditions(conditions);
            _state().or_conditions_.insert(_state().or_conditions_.end(), std::make_move_iterator(mc.begin()), std::make_move_iterator(mc.end()));
            return static_cast<Derived &>(*this);
        }

        Derived &Or(const std::string &query_string, const std::expected<SubqueryExpression, Error> &sub_expr_expected) {
            if (sub_expr_expected.has_value()) {
                _state().or_conditions_.emplace_back(query_string, std::vector<QueryValue>{sub_expr_expected.value()});
            } else {
#ifdef QT_CORE_LIB
                qWarning() << "OrMixin::Or(string, "
                              "expected<Subquery>): Subquery generation failed: "
                           << QString::fromStdString(sub_expr_expected.error().message) << ". Condition based on this subquery will not be added.";
#endif
            }
            return static_cast<Derived &>(*this);
        }

        template <typename T, std::enable_if_t<!std::is_same_v<std::decay_t<T>, QueryValue>, int> = 0>
        Derived &Or(const std::string &query_string, std::initializer_list<T> il) {
            std::vector<QueryValue> collected_args;
            collected_args.reserve(il.size());
            for (const auto &val : il) {
                collected_args.push_back(wrap_for_query_value(val));
            }
            _state().or_conditions_.emplace_back(query_string, std::move(collected_args));
            return static_cast<Derived &>(*this);
        }

        template <
            typename T,
            typename... TArgs,
            std::enable_if_t<!std::is_same_v<std::decay_t<T>, std::vector<QueryValue>> && !std::is_same_v<std::decay_t<T>, std::map<std::string, QueryValue>> && !detail::is_std_initializer_list<std::decay_t<T>>::value && !std::is_same_v<std::decay_t<T>, std::expected<SubqueryExpression, Error>>,
                             int> = 0>
        Derived &Or(const std::string &query_string, T &&val1, TArgs &&...vals_rest) {
            std::vector<QueryValue> collected_args;
            collected_args.reserve(1 + sizeof...(TArgs));
            collected_args.push_back(wrap_for_query_value(std::forward<T>(val1)));
            if constexpr (sizeof...(TArgs) > 0) {
                (collected_args.push_back(wrap_for_query_value(std::forward<TArgs>(vals_rest))), ...);
            }
            _state().or_conditions_.emplace_back(query_string, std::move(collected_args));
            return static_cast<Derived &>(*this);
        }
    };

}  // namespace cpporm

#endif  // cpporm_QUERY_BUILDER_OR_MIXIN_H