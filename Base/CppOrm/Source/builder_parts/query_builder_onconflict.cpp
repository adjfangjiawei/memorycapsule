// cpporm/builder_parts/query_builder_onconflict.cpp
#include <memory>  // For std::make_unique

#include "cpporm/builder_parts/query_builder_state.h"  // For OnConflictClause
#include "cpporm/query_builder_core.h"                 // For QueryBuilder definition and OnConflictUpdateSetter

namespace cpporm {

    QueryBuilder &QueryBuilder::OnConflictUpdateAllExcluded() {
        if (!state_.on_conflict_clause_) {
            state_.on_conflict_clause_ = std::make_unique<OnConflictClause>();
        }
        state_.on_conflict_clause_->action = OnConflictClause::Action::UpdateAllExcluded;
        state_.on_conflict_clause_->update_assignments.clear();
        state_.on_conflict_clause_->conflict_target_columns_db_names.clear();  // Reset target for this action
        return *this;
    }

    QueryBuilder &QueryBuilder::OnConflictDoNothing() {
        if (!state_.on_conflict_clause_) {
            state_.on_conflict_clause_ = std::make_unique<OnConflictClause>();
        }
        state_.on_conflict_clause_->action = OnConflictClause::Action::DoNothing;
        state_.on_conflict_clause_->update_assignments.clear();
        // For DO NOTHING, conflict_target_columns_db_names can be relevant for PG.
        // If the user wants a general DO NOTHING (e.g. MySQL INSERT IGNORE semantic),
        // they should not set targets. If they want PG "ON CONFLICT (target) DO NOTHING",
        // they'd typically call a method to set the target before or after this.
        // For now, this method doesn't clear the target.
        return *this;
    }

    QueryBuilder &QueryBuilder::OnConflictUpdateSpecific(std::function<void(OnConflictUpdateSetter &)> updater_fn) {
        if (!state_.on_conflict_clause_) {
            state_.on_conflict_clause_ = std::make_unique<OnConflictClause>();
        }
        state_.on_conflict_clause_->action = OnConflictClause::Action::UpdateSpecific;
        // Conflict target should be set by user if needed for PG before calling this,
        // or through a dedicated method. This method doesn't clear target.
        OnConflictUpdateSetter setter(*state_.on_conflict_clause_);
        updater_fn(setter);
        return *this;
    }

}  // namespace cpporm