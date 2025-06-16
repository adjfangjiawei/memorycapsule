// cpporm/session_builder_factories.cpp
#include "cpporm/model_base.h"    // For ModelBase, ModelMeta
#include "cpporm/query_builder.h" // 因为这些方法返回 QueryBuilder
#include "cpporm/session.h"       // 主头文件

namespace cpporm {

// --- Model/Table selection implementation ---
QueryBuilder Session::Model(const ModelBase *model_instance_hint) {
  if (!model_instance_hint) {
    return QueryBuilder(this, this->connection_name_, nullptr);
  }
  return QueryBuilder(this, this->connection_name_,
                      &(model_instance_hint->_getOwnModelMeta()));
}

QueryBuilder Session::Model(const ModelMeta &meta) {
  return QueryBuilder(this, this->connection_name_, &meta);
}

QueryBuilder Session::Table(const std::string &table_name) {
  QueryBuilder qb(this, this->connection_name_, nullptr);
  qb.Table(table_name);
  return qb;
}

QueryBuilder Session::MakeQueryBuilder() {
  return QueryBuilder(this, this->connection_name_, nullptr);
}

// --- OnConflict clause setters implementation ---
Session &Session::OnConflictUpdateAllExcluded() {
  if (!temp_on_conflict_clause_) {
    temp_on_conflict_clause_ = std::make_unique<OnConflictClause>();
  }
  temp_on_conflict_clause_->action =
      OnConflictClause::Action::UpdateAllExcluded;
  temp_on_conflict_clause_->update_assignments.clear();
  return *this;
}

Session &Session::OnConflictDoNothing() {
  if (!temp_on_conflict_clause_) {
    temp_on_conflict_clause_ = std::make_unique<OnConflictClause>();
  }
  temp_on_conflict_clause_->action = OnConflictClause::Action::DoNothing;
  temp_on_conflict_clause_->update_assignments.clear();
  return *this;
}

Session &Session::OnConflictUpdateSpecific(
    std::function<void(SessionOnConflictUpdateSetter &)> updater_fn) {
  if (!temp_on_conflict_clause_) {
    temp_on_conflict_clause_ = std::make_unique<OnConflictClause>();
  }
  SessionOnConflictUpdateSetter setter(*temp_on_conflict_clause_);
  updater_fn(setter);
  return *this;
}

} // namespace cpporm