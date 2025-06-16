#ifndef cpporm_QUERY_BUILDER_CLAUSES_MIXIN_H
#define cpporm_QUERY_BUILDER_CLAUSES_MIXIN_H

#include "cpporm/builder_parts/query_builder_conditions_mixin.h" // << 需要 wrap_for_query_value
#include "cpporm/builder_parts/query_builder_state.h"
#include <algorithm>
#include <initializer_list>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace cpporm {

template <typename Derived> class QueryBuilderClausesMixin {
protected:
  QueryBuilderState &_state() {
    return static_cast<Derived *>(this)->getState_();
  }
  const QueryBuilderState &_state() const {
    return static_cast<const Derived *>(this)->getState_();
  }

private:
  // 内部辅助函数：重置（清空）选择字段列表
  void reset_select_fields() { _state().select_fields_.clear(); }

  // 当添加具体的选择项时，如果当前仅有默认的 "*" 选择，则清除它
  void clear_default_select_if_adding_specifics() {
    if (_state().select_fields_.size() == 1 &&
        std::holds_alternative<std::string>(_state().select_fields_[0]) &&
        std::get<std::string>(_state().select_fields_[0]) == "*") {
      _state().select_fields_.clear();
    }
  }

  // 如果选择字段列表为空，则恢复为默认的 "*"
  void restore_default_select_if_empty() {
    if (_state().select_fields_.empty()) {
      _state().select_fields_.push_back(std::string("*"));
    }
  }

  // 内部辅助函数：添加一个选择字段变体到列表（不清除现有）
  // 主要用于公共 Select 方法内部，或者 AddSelect 方法
  void add_select_field_variant(SelectField field_variant) {
    bool found = false;
    if (std::holds_alternative<std::string>(field_variant)) {
      const std::string &field_str_to_add =
          std::get<std::string>(field_variant);
      if (field_str_to_add.empty())
        return;
      // DISTINCT 关键字不应通过 Select 添加，而是通过 Distinct() 方法
      // std::string temp_check_str = field_str_to_add;
      // std::transform(temp_check_str.begin(), temp_check_str.end(),
      //                temp_check_str.begin(), ::tolower);
      // if (temp_check_str == "distinct" && _state().apply_distinct_) {
      //   return; // Distinct() 方法已处理
      // }

      for (const auto &existing_field : _state().select_fields_) {
        if (std::holds_alternative<std::string>(existing_field) &&
            std::get<std::string>(existing_field) == field_str_to_add) {
          found = true; // 简单字符串去重
          break;
        }
      }
    } else if (std::holds_alternative<NamedSubqueryField>(field_variant)) {
      // 子查询通常不基于字符串内容去重，它们的别名可能不同
    }

    if (!found) {
      _state().select_fields_.push_back(std::move(field_variant));
    }
  }

  std::string trim_field_string(const std::string &field_str_with_spaces) {
    size_t first = field_str_with_spaces.find_first_not_of(" \t\n\r\f\v");
    if (std::string::npos == first)
      return "";
    size_t last = field_str_with_spaces.find_last_not_of(" \t\n\r\f\v");
    return field_str_with_spaces.substr(first, (last - first + 1));
  }

public:
  // Select 方法：这些方法应该 *替换* 当前的选择列表
  Derived &Select(const std::string &fields_string) {
    reset_select_fields(); // 清除所有之前的选择项
    std::string temp_field_str;
    std::stringstream ss(fields_string);
    while (std::getline(ss, temp_field_str, ',')) {
      std::string trimmed_field = trim_field_string(temp_field_str);
      if (!trimmed_field.empty()) {
        add_select_field_variant(trimmed_field); // 添加新的选择项
      }
    }
    restore_default_select_if_empty(); // 如果最终列表为空，恢复为 "*"
    return static_cast<Derived &>(*this);
  }

  Derived &Select(const std::vector<std::string> &fields_list) {
    reset_select_fields(); // 清除所有之前的选择项
    if (!fields_list.empty()) {
      for (const auto &field_str : fields_list) {
        std::string trimmed_field = trim_field_string(field_str);
        if (!trimmed_field.empty()) {
          add_select_field_variant(trimmed_field);
        }
      }
    }
    restore_default_select_if_empty();
    return static_cast<Derived &>(*this);
  }

  Derived &Select(std::initializer_list<std::string_view> fields_il) {
    reset_select_fields(); // 清除所有之前的选择项
    if (fields_il.size() > 0) {
      for (std::string_view sv : fields_il) {
        std::string trimmed_field = trim_field_string(std::string(sv));
        if (!trimmed_field.empty()) {
          add_select_field_variant(trimmed_field);
        }
      }
    }
    restore_default_select_if_empty();
    return static_cast<Derived &>(*this);
  }

  template <
      typename... Args,
      std::enable_if_t<
          std::conjunction_v<std::is_convertible<Args, std::string_view>...>,
          int> = 0>
  Derived &Select(std::string_view first_field, Args &&...rest_fields) {
    reset_select_fields(); // 清除所有之前的选择项
    std::string trimmed_first = trim_field_string(std::string(first_field));
    if (!trimmed_first.empty()) {
      add_select_field_variant(trimmed_first);
    }
    if constexpr (sizeof...(Args) > 0) {
      (
          (void)[&] {
            std::string trimmed_rest =
                trim_field_string(std::string(std::forward<Args>(rest_fields)));
            if (!trimmed_rest.empty()) {
              add_select_field_variant(trimmed_rest);
            }
          }(),
          ...);
    }
    restore_default_select_if_empty();
    return static_cast<Derived &>(*this);
  }

  // AddSelect 方法：这些方法 *追加* 到当前的选择列表
  Derived &AddSelect(const std::string &field_or_expr_string) {
    clear_default_select_if_adding_specifics(); // 如果当前仅为 "*", 则清除
    std::string trimmed_field = trim_field_string(field_or_expr_string);
    if (!trimmed_field.empty()) {
      add_select_field_variant(trimmed_field);
    }
    // restore_default_select_if_empty(); // AddSelect 不应该在添加后恢复为 "*",
    // 除非列表在清除 "*" 后仍为空
    if (_state().select_fields_.empty() &&
        field_or_expr_string
            .empty()) { // 仅当添加了一个空字符串且原先是*时才恢复
      restore_default_select_if_empty();
    } else if (_state().select_fields_.empty() &&
               !field_or_expr_string.empty()) {
      // 如果添加了有效字段后列表仍为空（理论上不应该，除非
      // add_select_field_variant 有bug），
      // 或者就是想添加一个有效的，那么不应该恢复为 *
    } else if (_state()
                   .select_fields_
                   .empty()) { // 最终如果还是空的（比如只添加了空字符串）
      restore_default_select_if_empty();
    }

    return static_cast<Derived &>(*this);
  }

  Derived &AddSelect(const NamedSubqueryField &subquery_field) {
    clear_default_select_if_adding_specifics();
    add_select_field_variant(subquery_field);
    // restore_default_select_if_empty(); // 同上，AddSelect 不应轻易恢复为 "*"
    if (_state().select_fields_.empty()) {
      restore_default_select_if_empty();
    }
    return static_cast<Derived &>(*this);
  }
  Derived &AddSelect(
      NamedSubqueryField &&subquery_field) { // Rvalue overload for efficiency
    clear_default_select_if_adding_specifics();
    add_select_field_variant(std::move(subquery_field));
    if (_state().select_fields_.empty()) {
      restore_default_select_if_empty();
    }
    return static_cast<Derived &>(*this);
  }

  Derived &Distinct(bool apply = true) {
    _state().apply_distinct_ = apply;
    return static_cast<Derived &>(*this);
  }

  Derived &Order(const std::string &order_string) {
    _state().order_clause_ = order_string;
    return static_cast<Derived &>(*this);
  }

  Derived &Limit(int limit_val_param) {
    _state().limit_val_ = limit_val_param;
    return static_cast<Derived &>(*this);
  }

  Derived &Offset(int offset_val_param) {
    _state().offset_val_ = offset_val_param;
    return static_cast<Derived &>(*this);
  }

  Derived &Group(const std::string &group_string) {
    _state().group_clause_ = group_string;
    return static_cast<Derived &>(*this);
  }

  // --- Having 方法 ---
  Derived &Having(const std::string &query_str,
                  const std::vector<QueryValue> &args) {
    _state().having_condition_ = std::make_unique<Condition>(query_str, args);
    return static_cast<Derived &>(*this);
  }

  Derived &Having(const std::string &query_str) {
    _state().having_condition_ =
        std::make_unique<Condition>(query_str, std::vector<QueryValue>{});
    return static_cast<Derived &>(*this);
  }

  Derived &Having(const std::string &query_str,
                  std::initializer_list<QueryValue> il) {
    _state().having_condition_ = std::make_unique<Condition>(query_str, il);
    return static_cast<Derived &>(*this);
  }

  template <typename T, typename... TArgs,
            std::enable_if_t<
                !std::is_same_v<std::decay_t<T>, std::vector<QueryValue>> &&
                    !detail::is_std_initializer_list<std::decay_t<T>>::value,
                int> = 0>
  Derived &Having(const std::string &query_str, T &&val1,
                  TArgs &&...vals_rest) {
    std::vector<QueryValue> collected_args;
    collected_args.reserve(1 + sizeof...(TArgs));
    collected_args.push_back(wrap_for_query_value(std::forward<T>(val1)));
    if constexpr (sizeof...(TArgs) > 0) {
      (collected_args.push_back(
           wrap_for_query_value(std::forward<TArgs>(vals_rest))),
       ...);
    }
    _state().having_condition_ =
        std::make_unique<Condition>(query_str, std::move(collected_args));
    return static_cast<Derived &>(*this);
  }
  // --- 结束 Having 方法 ---

  const std::string &getOrderClause_mixin() const {
    return _state().order_clause_;
  }
  int getLimitVal_mixin() const { return _state().limit_val_; }
  int getOffsetVal_mixin() const { return _state().offset_val_; }
  const std::string &getGroupClause_mixin() const {
    return _state().group_clause_;
  }
  const Condition *getHavingCondition_mixin() const {
    return _state().having_condition_ ? &(*_state().having_condition_)
                                      : nullptr;
  }
  bool isDistinctApplied_mixin() const { return _state().apply_distinct_; }
};

} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_CLAUSES_MIXIN_H