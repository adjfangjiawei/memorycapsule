#ifndef cpporm_QUERY_BUILDER_IN_MIXIN_H
#define cpporm_QUERY_BUILDER_IN_MIXIN_H

#include "cpporm/builder_parts/conditions/conditions_helpers.h"

namespace cpporm {

    template <typename Derived>
    class InMixin {
      protected:
        QueryBuilderState &_state() {
            return static_cast<Derived *>(this)->getState_();
        }

      public:
        Derived &In(const std::string &column_name, const std::vector<QueryValue> &values) {
            if (values.empty()) {
                _state().where_conditions_.emplace_back("1 = 0");
                return static_cast<Derived &>(*this);
            }
            std::string placeholders;
            placeholders.reserve(values.size() * 3);
            for (size_t i = 0; i < values.size(); ++i) {
                placeholders += (i == 0 ? "?" : ", ?");
            }
            std::string quoted_column = static_cast<Derived *>(this)->quoteSqlIdentifier(column_name);
            _state().where_conditions_.emplace_back(quoted_column + " IN (" + placeholders + ")", values);
            return static_cast<Derived &>(*this);
        }

        Derived &In(const std::string &column_name, std::vector<QueryValue> &&values) {
            if (values.empty()) {
                _state().where_conditions_.emplace_back("1 = 0");
                return static_cast<Derived &>(*this);
            }
            std::string placeholders;
            placeholders.reserve(values.size() * 3);
            for (size_t i = 0; i < values.size(); ++i) {
                placeholders += (i == 0 ? "?" : ", ?");
            }
            std::string quoted_column = static_cast<Derived *>(this)->quoteSqlIdentifier(column_name);
            _state().where_conditions_.emplace_back(quoted_column + " IN (" + placeholders + ")", std::move(values));
            return static_cast<Derived &>(*this);
        }

        template <typename T>
        Derived &In(const std::string &column_name, const std::vector<T> &values) {
            if (values.empty()) {
                _state().where_conditions_.emplace_back("1 = 0");
                return static_cast<Derived &>(*this);
            }
            std::vector<QueryValue> qv_values;
            qv_values.reserve(values.size());
            for (const auto &val : values) {
                qv_values.push_back(wrap_for_query_value(val));
            }
            return static_cast<Derived *>(this)->In(column_name, std::move(qv_values));
        }

        Derived &In(const std::string &column_name, std::initializer_list<QueryValue> il) {
            return static_cast<Derived *>(this)->In(column_name, std::vector<QueryValue>(il.begin(), il.end()));
        }

        template <typename T, std::enable_if_t<!std::is_same_v<std::decay_t<T>, QueryValue> && !std::is_same_v<std::decay_t<T>, SubqueryExpression>, int> = 0>
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
            return static_cast<Derived *>(this)->In(column_name, std::move(qv_values));
        }
    };

}  // namespace cpporm

#endif  // cpporm_QUERY_BUILDER_IN_MIXIN_H