// cpporm/session_types.h
#ifndef cpporm_SESSION_TYPES_H
#define cpporm_SESSION_TYPES_H

#include "cpporm/builder_parts/query_builder_state.h" // For OnConflictClause, QueryValue
#include <QString>
#include <QVariant>
#include <map>
#include <string>
#include <typeindex>

namespace cpporm {

class SessionOnConflictUpdateSetter {
public:
  explicit SessionOnConflictUpdateSetter(OnConflictClause &clause_ref);
  SessionOnConflictUpdateSetter &Set(const std::string &db_column_name,
                                     const QueryValue &value);
  SessionOnConflictUpdateSetter &
  Set(const std::map<std::string, QueryValue> &assignments);

private:
  OnConflictClause &clause_to_build_;
};

// Session 内部用于数据提取的结构体
namespace internal {
struct SessionModelDataForWrite {
  std::map<QString, QVariant> fields_to_write;
  std::map<QString, QVariant> primary_key_fields;
  bool has_auto_increment_pk = false;
  QString auto_increment_pk_name_db;
  std::string pk_cpp_name_for_autoincrement;
  std::type_index pk_cpp_type_for_autoincrement = typeid(void);
};
} // namespace internal

inline SessionOnConflictUpdateSetter::SessionOnConflictUpdateSetter(
    OnConflictClause &clause_ref)
    : clause_to_build_(clause_ref) {
  clause_to_build_.action = OnConflictClause::Action::UpdateSpecific;
}

inline SessionOnConflictUpdateSetter &
SessionOnConflictUpdateSetter::Set(const std::string &db_column_name,
                                   const QueryValue &value) {
  clause_to_build_.update_assignments[db_column_name] = value;
  return *this;
}

inline SessionOnConflictUpdateSetter &SessionOnConflictUpdateSetter::Set(
    const std::map<std::string, QueryValue> &assignments) {
  for (const auto &pair : assignments) {
    clause_to_build_.update_assignments[pair.first] = pair.second;
  }
  return *this;
}

} // namespace cpporm

#endif // cpporm_SESSION_TYPES_H