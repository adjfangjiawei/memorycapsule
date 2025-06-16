// cpporm/builder_parts/query_builder_state.cpp
#include "cpporm/builder_parts/query_builder_state.h"
#include "cpporm/query_builder.h" // For QueryBuilder::quoteSqlIdentifier

namespace cpporm {

// Definition for the free function mapToConditions
std::vector<Condition>
mapToConditions(const std::map<std::string, QueryValue> &condition_map) {
  std::vector<Condition> conditions_vec;
  conditions_vec.reserve(condition_map.size());
  for (const auto &pair : condition_map) {
    // For map conditions, the key is the column name, and the value is its
    // target. The query string becomes "column_name = ?" If the value is a
    // SubqueryExpression, it will be handled by build_condition_logic_internal
    // when QueryBuilder::toQVariant is called for the argument.
    // Here, we just construct the "column = ?" part.
    conditions_vec.emplace_back(QueryBuilder::quoteSqlIdentifier(pair.first) +
                                    " = ?",
                                std::vector<QueryValue>{pair.second});
  }
  return conditions_vec;
}

} // namespace cpporm