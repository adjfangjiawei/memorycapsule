#ifndef cpporm_QUERY_BUILDER_STATE_H
#define cpporm_QUERY_BUILDER_STATE_H

#include "cpporm/model_base.h"
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>
#include <QVariant>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace cpporm {

// 用于子查询绑定的 QueryValue 变体 (不包含 SubqueryExpression 自身以避免递归)
using QueryValueVariantForSubquery =
    std::variant<std::nullptr_t, int, long long, double, std::string, bool,
                 QDateTime, QDate, QTime, QByteArray>;

// 表示子查询表达式的结构体
struct SubqueryExpression {
  std::string sql_string;                             // 子查询的 SQL 语句
  std::vector<QueryValueVariantForSubquery> bindings; // 子查询的绑定参数

  // 构造函数
  SubqueryExpression(std::string s,
                     std::vector<QueryValueVariantForSubquery> b = {})
      : sql_string(std::move(s)), bindings(std::move(b)) {}

  // 默认的拷贝和移动语义
  SubqueryExpression(const SubqueryExpression &) = default;
  SubqueryExpression &operator=(const SubqueryExpression &) = default;
  SubqueryExpression(SubqueryExpression &&) = default;
  SubqueryExpression &operator=(SubqueryExpression &&) = default;
};

// 表示查询中参数值的类型 (std::variant 允许多种类型)
// 新增: 将 SubqueryExpression 作为 QueryValue 的一个可能类型
using QueryValue =
    std::variant<std::nullptr_t, int, long long, double, std::string, bool,
                 QDateTime, QDate, QTime, QByteArray, SubqueryExpression>;

// 表示查询条件的结构体 (例如 WHERE, HAVING 中的条件)
struct Condition {
  std::string query_string;     // 条件的 SQL 片段 (例如 "age > ?")
  std::vector<QueryValue> args; // 条件的绑定参数

  // 构造函数
  Condition(std::string qs, std::vector<QueryValue> a = {})
      : query_string(std::move(qs)), args(std::move(a)) {}
};

// 表示 JOIN 子句的结构体
struct JoinClause {
  std::string join_type;     // JOIN 类型 (例如 "INNER", "LEFT")
  std::string table_to_join; // 要连接的表名
  std::string on_condition;  // JOIN 的 ON 条件

  // 构造函数
  JoinClause(std::string type, std::string table, std::string on)
      : join_type(std::move(type)), table_to_join(std::move(table)),
        on_condition(std::move(on)) {}
};

// 表示预加载请求的结构体
struct PreloadRequest {
  std::string association_cpp_field_name; // 要预加载的关联字段的 C++ 名称

  // 构造函数
  explicit PreloadRequest(std::string name)
      : association_cpp_field_name(std::move(name)) {}
};

// 定义 ON CONFLICT (或 ON DUPLICATE KEY UPDATE) 子句的状态
struct OnConflictClause {
  enum class Action {
    DoNothing, // 例如 PostgreSQL 的 DO NOTHING 或 MySQL 的 INSERT IGNORE
    UpdateAllExcluded, // 更新所有非主键列为新插入的值 (MySQL: VALUES(col))
    UpdateSpecific     // 更新指定的列
  };

  Action action = Action::DoNothing; // 默认操作
  std::vector<std::string>
      conflict_target_columns_db_names; // 冲突目标列 (主要用于 PostgreSQL)
  std::map<std::string, QueryValue>
      update_assignments; // 指定更新时的列名和值映射

  // 构造函数
  OnConflictClause(Action act = Action::DoNothing) : action(act) {}
  // 默认的拷贝和移动语义
  OnConflictClause(const OnConflictClause &other) = default;
  OnConflictClause(OnConflictClause &&other) noexcept = default;
  OnConflictClause &operator=(const OnConflictClause &other) = default;
  OnConflictClause &operator=(OnConflictClause &&other) noexcept = default;
};

// 表示一个 Common Table Expression (CTE) 的状态
struct CTEState {
  std::string name;         // CTE 的名称
  SubqueryExpression query; // CTE 的定义查询 (使用 SubqueryExpression 结构)
  bool recursive = false;   // CTE 是否是递归的

  CTEState(std::string n, SubqueryExpression q, bool rec = false)
      : name(std::move(n)), query(std::move(q)), recursive(rec) {}

  // 默认的拷贝和移动语义
  CTEState(const CTEState &other) = default;
  CTEState(CTEState &&other) noexcept = default;
  CTEState &operator=(const CTEState &other) = default;
  CTEState &operator=(CTEState &&other) noexcept = default;
};

// --- New structures for enhanced subquery support ---
struct SubquerySource {
  SubqueryExpression subquery;
  std::string alias;

  SubquerySource(SubqueryExpression sq, std::string a)
      : subquery(std::move(sq)), alias(std::move(a)) {}
  // Default copy/move
  SubquerySource(const SubquerySource &) = default;
  SubquerySource &operator=(const SubquerySource &) = default;
  SubquerySource(SubquerySource &&) = default;
  SubquerySource &operator=(SubquerySource &&) = default;
};

// Represents the source for a FROM clause (either a table name or a subquery
// with an alias)
using FromClauseSource =
    std::variant<std::string /* table_name */, SubquerySource>;

struct NamedSubqueryField {
  SubqueryExpression subquery;
  std::string alias;

  NamedSubqueryField(SubqueryExpression sq, std::string a)
      : subquery(std::move(sq)), alias(std::move(a)) {}
  // Default copy/move
  NamedSubqueryField(const NamedSubqueryField &) = default;
  NamedSubqueryField &operator=(const NamedSubqueryField &) = default;
  NamedSubqueryField(NamedSubqueryField &&) = default;
  NamedSubqueryField &operator=(NamedSubqueryField &&) = default;
};

// Represents a field in the SELECT list (either a string or a named subquery)
using SelectField = std::variant<std::string /* field_name_or_expression */,
                                 NamedSubqueryField>;
// --- End of new structures ---

// QueryBuilder 的内部状态结构体, 存储所有查询构建部分
struct QueryBuilderState {
  const ModelMeta *model_meta_ = nullptr; // 指向当前操作模型的元数据
  FromClauseSource from_clause_source_{
      std::string("")}; // Default to empty table name string

  // 条件子句
  std::vector<Condition> where_conditions_; // WHERE 条件列表
  std::vector<Condition> or_conditions_;    // OR 条件列表
  std::vector<Condition> not_conditions_; // NOT 条件列表 (通常包装一组AND条件)

  // SELECT 子句相关
  std::vector<SelectField> select_fields_{
      std::string("*")};        // 要选择的字段列表 (默认 "*")
  bool apply_distinct_ = false; // 新增: 是否在 SELECT 后应用 DISTINCT

  std::string order_clause_;                    // ORDER BY 子句
  int limit_val_ = -1;                          // LIMIT 值 (-1 表示无限制)
  int offset_val_ = -1;                         // OFFSET 值 (-1 表示无偏移)
  std::string group_clause_;                    // GROUP BY 子句
  std::unique_ptr<Condition> having_condition_; // HAVING 条件

  // JOIN 子句
  std::vector<JoinClause> join_clauses_; // JOIN 子句列表

  // 预加载
  std::vector<PreloadRequest> preload_requests_; // 预加载请求列表

  // 作用域控制
  bool apply_soft_delete_scope_ = true; // 是否应用软删除作用域 (默认是)

  // OnConflict 子句
  std::unique_ptr<OnConflictClause> on_conflict_clause_; // ON CONFLICT 子句状态

  // Common Table Expressions (CTEs)
  std::vector<CTEState> ctes_; // WITH 子句列表

  // 默认构造函数
  QueryBuilderState() = default;

  // 拷贝构造函数 (深拷贝)
  QueryBuilderState(const QueryBuilderState &other)
      : model_meta_(other.model_meta_),
        from_clause_source_(other.from_clause_source_),
        where_conditions_(other.where_conditions_),
        or_conditions_(other.or_conditions_),
        not_conditions_(other.not_conditions_),
        select_fields_(other.select_fields_),
        apply_distinct_(other.apply_distinct_), // 拷贝 apply_distinct_
        order_clause_(other.order_clause_), limit_val_(other.limit_val_),
        offset_val_(other.offset_val_), group_clause_(other.group_clause_),
        join_clauses_(other.join_clauses_),
        preload_requests_(other.preload_requests_),
        apply_soft_delete_scope_(other.apply_soft_delete_scope_),
        ctes_(other.ctes_) {
    if (other.having_condition_) {
      having_condition_ = std::make_unique<Condition>(*other.having_condition_);
    }
    if (other.on_conflict_clause_) {
      on_conflict_clause_ =
          std::make_unique<OnConflictClause>(*other.on_conflict_clause_);
    }
  }

  // 移动构造函数
  QueryBuilderState(QueryBuilderState &&other) noexcept
      : model_meta_(other.model_meta_),
        from_clause_source_(std::move(other.from_clause_source_)),
        where_conditions_(std::move(other.where_conditions_)),
        or_conditions_(std::move(other.or_conditions_)),
        not_conditions_(std::move(other.not_conditions_)),
        select_fields_(std::move(other.select_fields_)),
        apply_distinct_(other.apply_distinct_), // 移动 apply_distinct_
        order_clause_(std::move(other.order_clause_)),
        limit_val_(other.limit_val_), offset_val_(other.offset_val_),
        group_clause_(std::move(other.group_clause_)),
        having_condition_(std::move(other.having_condition_)),
        join_clauses_(std::move(other.join_clauses_)),
        preload_requests_(std::move(other.preload_requests_)),
        apply_soft_delete_scope_(other.apply_soft_delete_scope_),
        on_conflict_clause_(std::move(other.on_conflict_clause_)),
        ctes_(std::move(other.ctes_)) {
    other.model_meta_ = nullptr;
    other.limit_val_ = -1;
    other.offset_val_ = -1;
    other.apply_distinct_ = false; // 重置源对象的 apply_distinct_
  }

  // 拷贝赋值运算符
  QueryBuilderState &operator=(const QueryBuilderState &other) {
    if (this == &other)
      return *this;

    model_meta_ = other.model_meta_;
    from_clause_source_ = other.from_clause_source_;
    where_conditions_ = other.where_conditions_;
    or_conditions_ = other.or_conditions_;
    not_conditions_ = other.not_conditions_;
    select_fields_ = other.select_fields_;
    apply_distinct_ = other.apply_distinct_; // 赋值 apply_distinct_
    order_clause_ = other.order_clause_;
    limit_val_ = other.limit_val_;
    offset_val_ = other.offset_val_;
    group_clause_ = other.group_clause_;
    if (other.having_condition_) {
      having_condition_ = std::make_unique<Condition>(*other.having_condition_);
    } else {
      having_condition_.reset();
    }
    join_clauses_ = other.join_clauses_;
    preload_requests_ = other.preload_requests_;
    apply_soft_delete_scope_ = other.apply_soft_delete_scope_;
    if (other.on_conflict_clause_) {
      on_conflict_clause_ =
          std::make_unique<OnConflictClause>(*other.on_conflict_clause_);
    } else {
      on_conflict_clause_.reset();
    }
    ctes_ = other.ctes_;
    return *this;
  }

  // 移动赋值运算符
  QueryBuilderState &operator=(QueryBuilderState &&other) noexcept {
    if (this == &other)
      return *this;

    model_meta_ = other.model_meta_;
    from_clause_source_ = std::move(other.from_clause_source_);
    where_conditions_ = std::move(other.where_conditions_);
    or_conditions_ = std::move(other.or_conditions_);
    not_conditions_ = std::move(other.not_conditions_);
    select_fields_ = std::move(other.select_fields_);
    apply_distinct_ = other.apply_distinct_; // 移动 apply_distinct_
    order_clause_ = std::move(other.order_clause_);
    limit_val_ = other.limit_val_;
    offset_val_ = other.offset_val_;
    group_clause_ = std::move(other.group_clause_);
    having_condition_ = std::move(other.having_condition_);
    join_clauses_ = std::move(other.join_clauses_);
    preload_requests_ = std::move(other.preload_requests_);
    apply_soft_delete_scope_ = other.apply_soft_delete_scope_;
    on_conflict_clause_ = std::move(other.on_conflict_clause_);
    ctes_ = std::move(other.ctes_);

    other.model_meta_ = nullptr;
    other.from_clause_source_ = std::string("");
    other.limit_val_ = -1;
    other.offset_val_ = -1;
    other.apply_distinct_ = false; // 重置源对象的 apply_distinct_
    return *this;
  }
};

std::vector<Condition>
mapToConditions(const std::map<std::string, QueryValue> &condition_map);

} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_STATE_H