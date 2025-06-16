// cpporm/builder_parts/query_builder_execution_non_template.cpp
#include "cpporm/i_query_executor.h"   // For IQueryExecutor
#include "cpporm/query_builder_core.h" // For QueryBuilder definition
#include "cpporm/session.h" // For Session::anyToQueryValueForSessionConvenience

namespace cpporm {

Error QueryBuilder::First(ModelBase &result_model) {
  if (!executor_)
    return Error(ErrorCode::InternalError, "QueryBuilder has no executor.");

  const ModelMeta &meta = result_model._getOwnModelMeta();
  if (this->state_.model_meta_ != &meta) {
    this->Model(meta);
  }

  std::map<std::string, QueryValue> pk_conditions;
  bool all_pks_set_in_model = true;
  if (meta.primary_keys_db_names.empty()) {
    all_pks_set_in_model = false;
  } else {
    for (const std::string &pk_db_name : meta.primary_keys_db_names) {
      const FieldMeta *pk_field = meta.findFieldByDbName(pk_db_name);
      if (!pk_field) {
        all_pks_set_in_model = false;
        break;
      }
      std::any pk_val_any = result_model.getFieldValue(pk_field->cpp_name);
      if (!pk_val_any.has_value()) {
        all_pks_set_in_model = false;
        break;
      }
      // Use Session's public static helper for conversion
      QueryValue qv = Session::anyToQueryValueForSessionConvenience(pk_val_any);

      bool use_this_pk_value = false;
      if (std::holds_alternative<int>(qv) && std::get<int>(qv) != 0)
        use_this_pk_value = true;
      else if (std::holds_alternative<long long>(qv) &&
               std::get<long long>(qv) != 0)
        use_this_pk_value = true;
      else if (std::holds_alternative<std::string>(qv) &&
               !std::get<std::string>(qv).empty())
        use_this_pk_value = true;
      // Add other QueryValue types if they can be PKs and have non-default
      // "empty" states

      if (use_this_pk_value) {
        pk_conditions[pk_db_name] = qv;
      } else {
        all_pks_set_in_model = false;
        break;
      }
    }
  }

  if (all_pks_set_in_model && !pk_conditions.empty()) {
    this->Where(
        pk_conditions); // This calls the mixin's Where or QB's own Where
  } else {
    // If not searching by PKs from the model, or if PKs were empty/default,
    // apply default ordering by PK for "First" if not already ordered.
    // This matches GORM's behavior of ordering by primary key when finding the
    // first record without explicit conditions.
    if (!meta.primary_keys_db_names.empty() &&
        this->state_.order_clause_.empty()) {
      std::string order_by_pk_clause;
      for (size_t i = 0; i < meta.primary_keys_db_names.size(); ++i) {
        order_by_pk_clause +=
            quoteSqlIdentifier( // Uses static QB::quoteSqlIdentifier
                meta.primary_keys_db_names[i]);
        if (i < meta.primary_keys_db_names.size() - 1)
          order_by_pk_clause += ", ";
      }
      if (!order_by_pk_clause.empty()) {
        this->Order(order_by_pk_clause); // Calls mixin's Order
      }
    }
  }
  return executor_->FirstImpl(*this, result_model);
}

Error QueryBuilder::Find(
    std::vector<std::unique_ptr<ModelBase>> &results_vector,
    std::function<std::unique_ptr<ModelBase>()> element_type_factory) {
  if (!executor_)
    return Error(ErrorCode::InternalError, "QueryBuilder has no executor.");

  if (!this->state_.model_meta_ && element_type_factory) {
    auto temp_instance = element_type_factory();
    if (temp_instance) {
      this->Model(temp_instance->_getOwnModelMeta()); // Calls QB's Model
    } else {
      return Error(ErrorCode::InternalError,
                   "Model factory returned nullptr for Find.");
    }
  } else if (!this->state_.model_meta_ && !element_type_factory) {
    return Error(ErrorCode::InvalidConfiguration,
                 "Find requires ModelMeta or an element factory.");
  }

  return executor_->FindImpl(*this, results_vector, element_type_factory);
}

std::expected<QVariant, Error>
QueryBuilder::Create(ModelBase &model,
                     const OnConflictClause *conflict_options_override) {
  if (!executor_)
    return std::unexpected(
        Error(ErrorCode::InternalError, "QueryBuilder has no executor."));

  if (!this->state_.model_meta_ ||
      this->state_.model_meta_ != &model._getOwnModelMeta()) {
    this->Model(model._getOwnModelMeta());
  }
  const OnConflictClause *final_conflict_options = conflict_options_override;
  if (!final_conflict_options && this->state_.on_conflict_clause_) {
    final_conflict_options = this->state_.on_conflict_clause_.get();
  }
  return executor_->CreateImpl(*this, model, final_conflict_options);
}

std::expected<long long, Error>
QueryBuilder::Updates(const std::map<std::string, QueryValue> &updates) {
  if (!executor_)
    return std::unexpected(
        Error(ErrorCode::InternalError, "QueryBuilder has no executor."));
  if (!this->state_.model_meta_ &&
      (this->state_.from_clause_source_.index() == 0 &&
       std::get<std::string>(this->state_.from_clause_source_).empty())) {
    return std::unexpected(
        Error(ErrorCode::InvalidConfiguration,
              "Updates requires a Model or Table to be set."));
  }
  return executor_->UpdatesImpl(*this, updates);
}

std::expected<long long, Error> QueryBuilder::Delete() {
  if (!executor_)
    return std::unexpected(
        Error(ErrorCode::InternalError, "QueryBuilder has no executor."));
  if (!this->state_.model_meta_ &&
      (this->state_.from_clause_source_.index() == 0 &&
       std::get<std::string>(this->state_.from_clause_source_).empty())) {
    return std::unexpected(
        Error(ErrorCode::InvalidConfiguration,
              "Delete requires a Model or Table to be set."));
  }
  return executor_->DeleteImpl(*this);
}

std::expected<long long, Error> QueryBuilder::Save(ModelBase &model) {
  if (!executor_)
    return std::unexpected(
        Error(ErrorCode::InternalError, "QueryBuilder has no executor."));

  if (!this->state_.model_meta_ ||
      this->state_.model_meta_ != &model._getOwnModelMeta()) {
    this->Model(model._getOwnModelMeta());
  }
  return executor_->SaveImpl(*this, model);
}

std::expected<int64_t, Error> QueryBuilder::Count() {
  if (!executor_)
    return std::unexpected(
        Error(ErrorCode::InternalError, "QueryBuilder has no executor."));
  if (!this->state_.model_meta_ &&
      (this->state_.from_clause_source_.index() == 0 &&
       std::get<std::string>(this->state_.from_clause_source_).empty())) {
    return std::unexpected(Error(ErrorCode::InvalidConfiguration,
                                 "Count requires a Model or Table to be set."));
  }
  return executor_->CountImpl(*this);
}

} // namespace cpporm