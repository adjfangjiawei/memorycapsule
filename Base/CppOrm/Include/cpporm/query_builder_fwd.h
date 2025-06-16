// cpporm/query_builder_fwd.h
#ifndef cpporm_QUERY_BUILDER_FWD_H
#define cpporm_QUERY_BUILDER_FWD_H

namespace cpporm {
class QueryBuilder;
class OnConflictUpdateSetter; // 也前向声明
struct OnConflictClause;      // 如果 setter 会用到
struct SubqueryExpression;    // 如果 setter 会用到
// struct QueryValue;            // 如果 setter 会用到
} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_FWD_H