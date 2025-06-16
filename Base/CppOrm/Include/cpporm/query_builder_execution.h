// cpporm/query_builder_execution.h
#ifndef cpporm_QUERY_BUILDER_EXECUTION_H
#define cpporm_QUERY_BUILDER_EXECUTION_H

#include "cpporm/query_builder_core.h"
// Other necessary includes are transitively included

namespace cpporm {

// --- Templated Execution Method Implementations ---

template <typename T> inline Error QueryBuilder::First(T *result_model) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  if (!executor_)
    return Error(ErrorCode::InternalError,
                 "QueryBuilder has no executor for First operation.");
  if (!result_model)
    return Error(ErrorCode::InternalError,
                 "Result model pointer is null for QueryBuilder::First.");

  if (!this->state_.model_meta_ ||
      this->state_.model_meta_ != &(T::getModelMeta())) {
    this->Model<T>();
  }
  // FirstImpl in Session will handle Limit(1) if appropriate
  return executor_->FirstImpl(*this, *static_cast<ModelBase *>(result_model));
}

template <typename T>
inline Error QueryBuilder::First(T *result_model,
                                 const QueryValue &primary_key_value) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  // No direct executor check here, First(T*) above will do it.
  this->Model<T>();
  const ModelMeta *meta = this->state_.model_meta_;
  if (!meta || meta->primary_keys_db_names.empty())
    return Error(ErrorCode::MappingError,
                 "Model has no primary key defined for First by PK.");
  if (meta->primary_keys_db_names.size() > 1)
    return Error(ErrorCode::InvalidConfiguration,
                 "Model has composite PKs. Use vector<QueryValue> or map "
                 "overload for First by PK.");
  // Correctly call Where with string and arguments
  this->Where(quoteSqlIdentifier(meta->primary_keys_db_names[0]) + " = ?",
              {primary_key_value});
  return this->First(result_model);
}

template <typename T>
inline Error
QueryBuilder::First(T *result_model,
                    const std::vector<QueryValue> &primary_key_values) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  this->Model<T>();
  const ModelMeta *meta = this->state_.model_meta_;
  if (!meta || meta->primary_keys_db_names.empty())
    return Error(ErrorCode::MappingError,
                 "Model has no primary keys defined for First by PKs.");
  if (meta->primary_keys_db_names.size() != primary_key_values.size())
    return Error(
        ErrorCode::InvalidConfiguration,
        "Number of PK values does not match PK columns for First by PKs.");
  std::map<std::string, QueryValue> conditions;
  for (size_t i = 0; i < meta->primary_keys_db_names.size(); ++i) {
    conditions[meta->primary_keys_db_names[i]] = primary_key_values[i];
  }
  this->Where(conditions); // Correctly call Where with a map
  return this->First(result_model);
}

template <typename T>
inline Error
QueryBuilder::First(T *result_model,
                    const std::map<std::string, QueryValue> &conditions) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  this->Model<T>();
  this->Where(conditions); // Correctly call Where with a map
  return this->First(result_model);
}

template <typename T>
inline Error QueryBuilder::Find(std::vector<T> *results_vector) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  if (!executor_)
    return Error(ErrorCode::InternalError,
                 "QueryBuilder has no executor for Find operation.");
  if (!results_vector)
    return Error(ErrorCode::InternalError,
                 "Results vector pointer is null for QueryBuilder::Find.");

  if (!this->state_.model_meta_ ||
      this->state_.model_meta_ != &(T::getModelMeta())) {
    this->Model<T>();
  }

  std::vector<std::unique_ptr<ModelBase>> base_results;
  auto factory = []() -> std::unique_ptr<ModelBase> {
    return std::make_unique<T>();
  };
  Error err = executor_->FindImpl(*this, base_results, factory);
  if (err) {
    return err;
  }

  results_vector->clear();
  results_vector->reserve(base_results.size());
  for (auto &base_ptr : base_results) {
    if (base_ptr) {
      T *typed_ptr = static_cast<T *>(base_ptr.release());
      results_vector->push_back(std::move(*typed_ptr));
      delete typed_ptr;
    }
  }
  return make_ok();
}

template <typename T>
inline Error
QueryBuilder::Find(std::vector<T> *results_vector,
                   const std::map<std::string, QueryValue> &conditions) {
  this->Model<T>();
  this->Where(conditions); // Correctly call Where with a map
  return this->Find(results_vector);
}

template <typename T>
inline Error QueryBuilder::Find(std::vector<T> *results_vector,
                                const std::string &query_string,
                                const std::vector<QueryValue> &args) {
  this->Model<T>();
  this->Where(query_string,
              args); // Correctly call Where with string and vector
  return this->Find(results_vector);
}

template <typename T>
inline Error
QueryBuilder::Find(std::vector<std::unique_ptr<T>> *results_vector) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  if (!executor_)
    return Error(ErrorCode::InternalError,
                 "QueryBuilder has no executor for Find operation.");
  if (!results_vector)
    return Error(ErrorCode::InternalError,
                 "Results vector pointer is null for QueryBuilder::Find.");

  if (!this->state_.model_meta_ ||
      this->state_.model_meta_ != &(T::getModelMeta())) {
    this->Model<T>();
  }

  std::vector<std::unique_ptr<ModelBase>> base_results;
  auto factory = []() -> std::unique_ptr<ModelBase> {
    return std::make_unique<T>();
  };
  Error err = executor_->FindImpl(*this, base_results, factory);
  if (err) {
    return err;
  }

  results_vector->clear();
  results_vector->reserve(base_results.size());
  for (auto &base_ptr : base_results) {
    results_vector->emplace_back(static_cast<T *>(base_ptr.release()));
  }
  return make_ok();
}

template <typename T>
inline Error
QueryBuilder::Find(std::vector<std::unique_ptr<T>> *results_vector,
                   const std::map<std::string, QueryValue> &conditions) {
  this->Model<T>();
  this->Where(conditions); // Correctly call Where with a map
  return this->Find(results_vector);
}

template <typename T>
inline Error QueryBuilder::Find(std::vector<std::unique_ptr<T>> *results_vector,
                                const std::string &query_string,
                                const std::vector<QueryValue> &args) {
  this->Model<T>();
  this->Where(query_string,
              args); // Correctly call Where with string and vector
  return this->Find(results_vector);
}

template <typename TModel>
inline std::expected<QVariant, Error> QueryBuilder::Create(TModel &model) {
  static_assert(std::is_base_of<ModelBase, TModel>::value,
                "TModel must be a descendant of cpporm::ModelBase");
  return this->Create(static_cast<ModelBase &>(model), nullptr);
}

template <typename TModel>
inline std::expected<long long, Error> QueryBuilder::Save(TModel &model) {
  static_assert(std::is_base_of<ModelBase, TModel>::value,
                "TModel must be a descendant of cpporm::ModelBase");
  return this->Save(static_cast<ModelBase &>(model));
}

} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_EXECUTION_H