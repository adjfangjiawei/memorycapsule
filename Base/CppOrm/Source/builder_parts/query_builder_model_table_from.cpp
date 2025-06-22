// cpporm/builder_parts/query_builder_model_table_from.cpp
#include <QDebug>  // For qWarning, if used by From(QueryBuilder&)

#include "cpporm/model_base.h"
#include "cpporm/query_builder_core.h"  // For QueryBuilder definition

namespace cpporm {

    // --- 核心 Model/Table/From 设置器 ---
    QueryBuilder &QueryBuilder::Model(const ModelBase *model_instance_hint) {
        if (model_instance_hint) {
            const ModelMeta &meta = model_instance_hint->_getOwnModelMeta();
            this->state_.model_meta_ = &meta;
            if (!meta.table_name.empty()) {
                this->state_.from_clause_source_ = meta.table_name;
            } else {
                this->state_.from_clause_source_ = std::string("");
            }
        } else {
            this->state_.model_meta_ = nullptr;
            this->state_.from_clause_source_ = std::string("");
        }
        return *this;
    }

    QueryBuilder &QueryBuilder::Model(const ModelMeta &meta) {
        this->state_.model_meta_ = &meta;
        if (!meta.table_name.empty()) {
            this->state_.from_clause_source_ = meta.table_name;
        } else {
            this->state_.from_clause_source_ = std::string("");
        }
        return *this;
    }

    QueryBuilder &QueryBuilder::Table(std::string table_name) {
        this->state_.from_clause_source_ = std::move(table_name);
        if (this->state_.model_meta_ && this->state_.model_meta_->table_name != std::get<std::string>(this->state_.from_clause_source_)) {
            this->state_.model_meta_ = nullptr;
        }
        return *this;
    }

    QueryBuilder &QueryBuilder::From(std::string source_name_or_cte_alias) {
        // std::string old_from_source_string_if_any; // Not used in current logic
        // if (std::holds_alternative<std::string>(this->state_.from_clause_source_)) {
        //   old_from_source_string_if_any =
        //       std::get<std::string>(this->state_.from_clause_source_);
        // }

        this->state_.from_clause_source_ = std::move(source_name_or_cte_alias);

        if (this->state_.model_meta_) {
            // If the new source is a string (table or CTE alias)
            if (std::holds_alternative<std::string>(this->state_.from_clause_source_)) {
                const std::string &new_from_str = std::get<std::string>(this->state_.from_clause_source_);
                bool is_known_cte = false;
                for (const auto &cte_def : this->state_.ctes_) {
                    if (cte_def.name == new_from_str) {
                        is_known_cte = true;
                        break;
                    }
                }
                // If it's a CTE or the new table name doesn't match the model's table name,
                // then the model context is no longer valid.
                if (is_known_cte || (this->state_.model_meta_->table_name != new_from_str && !new_from_str.empty())) {
                    this->state_.model_meta_ = nullptr;
                }
            } else {  // If the new source is a subquery, model_meta_ is definitely not applicable
                this->state_.model_meta_ = nullptr;
            }
        }
        return *this;
    }

    QueryBuilder &QueryBuilder::From(const QueryBuilder &subquery_builder, const std::string &alias) {
        auto sub_expr_expected = subquery_builder.AsSubquery();
        if (!sub_expr_expected.has_value()) {
            qWarning() << "cpporm QueryBuilder::From(QueryBuilder): Failed to create "
                          "subquery expression: "
                       << QString::fromStdString(sub_expr_expected.error().message);
            return *this;
        }
        this->state_.from_clause_source_ = SubquerySource{std::move(sub_expr_expected.value()), alias};
        this->state_.model_meta_ = nullptr;  // Setting FROM to a subquery invalidates model context
        return *this;
    }

    QueryBuilder &QueryBuilder::From(const SubqueryExpression &subquery_expr, const std::string &alias) {
        this->state_.from_clause_source_ = SubquerySource{subquery_expr, alias};
        this->state_.model_meta_ = nullptr;  // Setting FROM to a subquery invalidates model context
        return *this;
    }

}  // namespace cpporm