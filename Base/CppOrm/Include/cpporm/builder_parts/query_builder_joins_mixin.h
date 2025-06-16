#ifndef cpporm_QUERY_BUILDER_JOINS_MIXIN_H
#define cpporm_QUERY_BUILDER_JOINS_MIXIN_H

#include "cpporm/builder_parts/query_builder_state.h" // For QueryBuilderState, JoinClause
#include <QDebug>    // For qWarning (optional, for join parsing warning)
#include <algorithm> // For std::transform
#include <string>
#include <vector>

namespace cpporm {

template <typename Derived> class QueryBuilderJoinsMixin {
protected:
  QueryBuilderState &_state() {
    return static_cast<Derived *>(this)->getState_();
  }
  const QueryBuilderState &_state() const {
    return static_cast<const Derived *>(this)->getState_();
  }

public:
  Derived &Joins(const std::string &join_str) {
    std::string upper_join = join_str;
    // Ensure cpporm namespace for toupper or use ::toupper
    std::transform(upper_join.begin(), upper_join.end(), upper_join.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    if (upper_join.rfind("LEFT JOIN ", 0) == 0) {
      _state().join_clauses_.emplace_back("LEFT", "", join_str);
    } else if (upper_join.rfind("RIGHT JOIN ", 0) == 0) {
      _state().join_clauses_.emplace_back("RIGHT", "", join_str);
    } else if (upper_join.rfind("INNER JOIN ", 0) == 0) {
      _state().join_clauses_.emplace_back("INNER", "", join_str);
    } else if (upper_join.rfind("FULL JOIN ", 0) == 0) {
      _state().join_clauses_.emplace_back("FULL", "", join_str);
    } else if (upper_join.rfind("JOIN ", 0) == 0) {
      _state().join_clauses_.emplace_back(
          "INNER", "", join_str); // Default JOIN is usually INNER
    } else {
// qWarning() is a Qt function, ensure QDebug is included if used.
// For a library, consider a more generic logging/warning mechanism or none
// here.
#if defined(QT_CORE_LIB) // Only use qWarning if Qt is available
      qWarning() << "cpporm QueryBuilder::JoinsMixin: Could not determine "
                    "explicit join type from '"
                 << join_str.c_str() << "'. Storing as raw fragment.";
#endif
      _state().join_clauses_.emplace_back(
          "", "", join_str); // Empty type, just the raw string
    }
    return static_cast<Derived &>(*this);
  }

  Derived &InnerJoin(const std::string &table,
                     const std::string &on_condition) {
    _state().join_clauses_.emplace_back("INNER", table, on_condition);
    return static_cast<Derived &>(*this);
  }

  Derived &LeftJoin(const std::string &table, const std::string &on_condition) {
    _state().join_clauses_.emplace_back("LEFT", table, on_condition);
    return static_cast<Derived &>(*this);
  }

  Derived &RightJoin(const std::string &table,
                     const std::string &on_condition) {
    _state().join_clauses_.emplace_back("RIGHT", table, on_condition);
    return static_cast<Derived &>(*this);
  }

  // Accessor
  const std::vector<JoinClause> &getJoinClauses_mixin() const {
    return _state().join_clauses_;
  }
};

} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_JOINS_MIXIN_H