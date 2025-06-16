// cpporm/i_query_executor.h
#ifndef cpporm_I_QUERY_EXECUTOR_H
#define cpporm_I_QUERY_EXECUTOR_H

#include "cpporm/error.h"
// 直接包含 query_builder_state.h 来获取 QueryValue 和 OnConflictClause 的定义
#include "cpporm/builder_parts/query_builder_state.h"
#include "cpporm/model_base.h" // ModelBase, ModelMeta (ModelBase 也会包含 query_builder_state.h)

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace cpporm {

class QueryBuilder; // 前向声明 QueryBuilder

// QueryValue 和 OnConflictClause 现在通过包含 query_builder_state.h
// 来确保其定义可见

class IQueryExecutor {
public:
  virtual ~IQueryExecutor() = default;

  virtual Error FirstImpl(const QueryBuilder &qb, ModelBase &result_model) = 0;

  virtual Error FindImpl(
      const QueryBuilder &qb,
      std::vector<std::unique_ptr<ModelBase>> &results_vector,
      std::function<std::unique_ptr<ModelBase>()> element_type_factory) = 0;

  virtual std::expected<QVariant, Error>
  CreateImpl(const QueryBuilder &qb, ModelBase &model,
             const OnConflictClause *conflict_options_override) = 0;

  virtual std::expected<long long, Error>
  UpdatesImpl(const QueryBuilder &qb,
              const std::map<std::string, QueryValue> &updates) = 0;

  virtual std::expected<long long, Error>
  DeleteImpl(const QueryBuilder &qb) = 0;

  virtual std::expected<long long, Error> SaveImpl(const QueryBuilder &qb,
                                                   ModelBase &model) = 0;

  virtual std::expected<int64_t, Error> CountImpl(const QueryBuilder &qb) = 0;
};

} // namespace cpporm
#endif // cpporm_I_QUERY_EXECUTOR_H