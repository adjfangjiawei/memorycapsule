#ifndef cpporm_SESSION_TYPES_H
#define cpporm_SESSION_TYPES_H

#include <QString>   // QVariant 依赖 QString
#include <QVariant>  // QueryValue 仍然可以持有 QVariant 支持的类型
#include <map>
#include <string>
#include <typeindex>

#include "cpporm/builder_parts/query_builder_state.h"  // For OnConflictClause, QueryValue
#include "sqldriver/sql_value.h"                       // 使用 SqlValue

namespace cpporm {

    class SessionOnConflictUpdateSetter {
      public:
        explicit SessionOnConflictUpdateSetter(OnConflictClause &clause_ref);
        SessionOnConflictUpdateSetter &Set(const std::string &db_column_name, const QueryValue &value);
        SessionOnConflictUpdateSetter &Set(const std::map<std::string, QueryValue> &assignments);

      private:
        OnConflictClause &clause_to_build_;
    };

    // Session 内部用于数据提取的结构体
    namespace internal {
        struct SessionModelDataForWrite {
            // fields_to_write 将从 QVariant 改为 SqlValue
            std::map<std::string, cpporm_sqldriver::SqlValue> fields_to_write;
            // primary_key_fields 也改为 SqlValue
            std::map<std::string, cpporm_sqldriver::SqlValue> primary_key_fields;
            bool has_auto_increment_pk = false;
            std::string auto_increment_pk_name_db;      // 已经是 std::string
            std::string pk_cpp_name_for_autoincrement;  // 已经是 std::string
            std::type_index pk_cpp_type_for_autoincrement = typeid(void);
        };
    }  // namespace internal

    inline SessionOnConflictUpdateSetter::SessionOnConflictUpdateSetter(OnConflictClause &clause_ref) : clause_to_build_(clause_ref) {
        clause_to_build_.action = OnConflictClause::Action::UpdateSpecific;
    }

    inline SessionOnConflictUpdateSetter &SessionOnConflictUpdateSetter::Set(const std::string &db_column_name, const QueryValue &value) {
        clause_to_build_.update_assignments[db_column_name] = value;
        return *this;
    }

    inline SessionOnConflictUpdateSetter &SessionOnConflictUpdateSetter::Set(const std::map<std::string, QueryValue> &assignments) {
        for (const auto &pair : assignments) {
            clause_to_build_.update_assignments[pair.first] = pair.second;
        }
        return *this;
    }

}  // namespace cpporm

#endif  // cpporm_SESSION_TYPES_H