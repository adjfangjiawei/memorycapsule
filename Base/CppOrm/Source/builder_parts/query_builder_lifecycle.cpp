// cpporm/builder_parts/query_builder_lifecycle.cpp
#include "cpporm/query_builder_core.h"  // For QueryBuilder definition

namespace cpporm {

    // 构造函数接收 std::string connection_name
    QueryBuilder::QueryBuilder(IQueryExecutor *executor, std::string connection_name, const ModelMeta *model_meta) : executor_(executor), connection_name_(std::move(connection_name)) {
        if (model_meta) {
            this->state_.model_meta_ = model_meta;
            if (!model_meta->table_name.empty()) {
                this->state_.from_clause_source_ = model_meta->table_name;
            }
        }
        // Default select_fields_ is already "*" in QueryBuilderState constructor
    }

    QueryBuilder::QueryBuilder(const QueryBuilder &other) : executor_(other.executor_), connection_name_(other.connection_name_), state_(other.state_) {
    }

    QueryBuilder &QueryBuilder::operator=(const QueryBuilder &other) {
        if (this != &other) {
            this->executor_ = other.executor_;
            this->connection_name_ = other.connection_name_;
            this->state_ = other.state_;
        }
        return *this;
    }

    QueryBuilder::QueryBuilder(QueryBuilder &&other) noexcept : executor_(other.executor_), connection_name_(std::move(other.connection_name_)), state_(std::move(other.state_)) {
        other.executor_ = nullptr;
    }

    QueryBuilder &QueryBuilder::operator=(QueryBuilder &&other) noexcept {
        if (this != &other) {
            this->executor_ = other.executor_;
            this->connection_name_ = std::move(other.connection_name_);
            this->state_ = std::move(other.state_);
            other.executor_ = nullptr;
        }
        return *this;
    }

    QueryBuilder::~QueryBuilder() = default;

}  // namespace cpporm