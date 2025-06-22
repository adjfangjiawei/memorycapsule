#include "cpporm/model_base.h"     // For ModelBase, ModelMeta
#include "cpporm/query_builder.h"  // QueryBuilder 构造函数已更新
#include "cpporm/session.h"        // 主头文件

namespace cpporm {

    // --- Model/Table selection implementation ---
    QueryBuilder Session::Model(const ModelBase *model_instance_hint) {
        if (!model_instance_hint) {
            // QueryBuilder 构造函数接收 std::string connection_name_
            return QueryBuilder(this, this->connection_name_, nullptr);
        }
        // QueryBuilder 构造函数接收 std::string connection_name_
        return QueryBuilder(this, this->connection_name_, &(model_instance_hint->_getOwnModelMeta()));
    }

    QueryBuilder Session::Model(const ModelMeta &meta) {
        // QueryBuilder 构造函数接收 std::string connection_name_
        return QueryBuilder(this, this->connection_name_, &meta);
    }

    QueryBuilder Session::Table(const std::string &table_name) {
        // QueryBuilder 构造函数接收 std::string connection_name_
        QueryBuilder qb(this, this->connection_name_, nullptr);
        qb.Table(table_name);  // QueryBuilder::Table takes std::string
        return qb;
    }

    QueryBuilder Session::MakeQueryBuilder() {
        // QueryBuilder 构造函数接收 std::string connection_name_
        return QueryBuilder(this, this->connection_name_, nullptr);
    }

    // --- OnConflict clause setters implementation ---
    Session &Session::OnConflictUpdateAllExcluded() {
        if (!temp_on_conflict_clause_) {
            temp_on_conflict_clause_ = std::make_unique<OnConflictClause>();
        }
        temp_on_conflict_clause_->action = OnConflictClause::Action::UpdateAllExcluded;
        temp_on_conflict_clause_->update_assignments.clear();
        temp_on_conflict_clause_->conflict_target_columns_db_names.clear();  // 确保目标也清除
        return *this;
    }

    Session &Session::OnConflictDoNothing() {
        if (!temp_on_conflict_clause_) {
            temp_on_conflict_clause_ = std::make_unique<OnConflictClause>();
        }
        temp_on_conflict_clause_->action = OnConflictClause::Action::DoNothing;
        temp_on_conflict_clause_->update_assignments.clear();
        // conflict_target_columns_db_names 保持不变，用户可能已设置
        return *this;
    }

    Session &Session::OnConflictUpdateSpecific(std::function<void(SessionOnConflictUpdateSetter &)> updater_fn) {
        if (!temp_on_conflict_clause_) {
            temp_on_conflict_clause_ = std::make_unique<OnConflictClause>();
        }
        // SessionOnConflictUpdateSetter 构造函数会设置 action = UpdateSpecific
        SessionOnConflictUpdateSetter setter(*temp_on_conflict_clause_);
        updater_fn(setter);
        return *this;
    }

}  // namespace cpporm