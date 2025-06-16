// cpporm/session_crud_ops.h
#ifndef cpporm_SESSION_CRUD_OPS_H
#define cpporm_SESSION_CRUD_OPS_H

#include "cpporm/session_core.h"

namespace cpporm {

// --- 实现简单的模板化便捷 CRUD 方法 ---

template <typename TModel>
inline std::expected<QVariant, Error> Session::Create(TModel &model) {
  static_assert(std::is_base_of<ModelBase, TModel>::value,
                "TModel must be a descendant of cpporm::ModelBase");
  return this->Create(static_cast<ModelBase &>(model), nullptr);
}

template <typename T>
inline Error Session::First(T *result_model, QueryBuilder qb) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  // 移除 QB executor 的检查，因为这更像是 QB 自身方法的责任，
  // 或者 Session 在调用 QB 方法前应确保 QB 的状态。
  // Session 级别的 First(T*, QB) 应该信任传入的 QB。
  return qb.First(result_model);
}

template <typename T> inline Error Session::First(T *result_model) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().First(result_model);
}

template <typename T>
inline Error Session::First(T *result_model,
                            const QueryValue &primary_key_value) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().First(result_model, primary_key_value);
}

template <typename T>
inline Error Session::First(T *result_model,
                            const std::vector<QueryValue> &primary_key_values) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().First(result_model, primary_key_values);
}

template <typename T>
inline Error
Session::First(T *result_model,
               const std::map<std::string, QueryValue> &conditions) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().Where(conditions).First(result_model);
}

template <typename T>
inline Error Session::Find(std::vector<T> *results_vector, QueryBuilder qb) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return qb.Find(results_vector);
}

template <typename T>
inline Error Session::Find(std::vector<T> *results_vector) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().Find(results_vector);
}

template <typename T>
inline Error
Session::Find(std::vector<T> *results_vector,
              const std::map<std::string, QueryValue> &conditions) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().Where(conditions).Find(results_vector);
}

template <typename T>
inline Error Session::Find(std::vector<T> *results_vector,
                           const std::string &query_string,
                           const std::vector<QueryValue> &args) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().Where(query_string, args).Find(results_vector);
}

template <typename T>
inline Error Session::Find(std::vector<std::unique_ptr<T>> *results_vector,
                           QueryBuilder qb) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return qb.Find(results_vector);
}

template <typename T>
inline Error Session::Find(std::vector<std::unique_ptr<T>> *results_vector) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().Find(results_vector);
}

template <typename T>
inline Error
Session::Find(std::vector<std::unique_ptr<T>> *results_vector,
              const std::map<std::string, QueryValue> &conditions) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().Where(conditions).Find(results_vector);
}

template <typename T>
inline Error Session::Find(std::vector<std::unique_ptr<T>> *results_vector,
                           const std::string &query_string,
                           const std::vector<QueryValue> &args) {
  static_assert(std::is_base_of<ModelBase, T>::value,
                "T must be a descendant of cpporm::ModelBase");
  return this->Model<T>().Where(query_string, args).Find(results_vector);
}

template <typename TModel>
inline std::expected<long long, Error> Session::Save(TModel &model) {
  static_assert(std::is_base_of<ModelBase, TModel>::value,
                "TModel must be a descendant of cpporm::ModelBase");
  return this->Save(static_cast<ModelBase &>(model));
}

} // namespace cpporm

#endif // cpporm_SESSION_CRUD_OPS_H