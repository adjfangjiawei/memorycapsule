#ifndef cpporm_QUERY_BUILDER_CONDITIONS_MIXIN_H
#define cpporm_QUERY_BUILDER_CONDITIONS_MIXIN_H

#include "cpporm/builder_parts/conditions/in_mixin.h"
#include "cpporm/builder_parts/conditions/not_mixin.h"
#include "cpporm/builder_parts/conditions/or_mixin.h"
#include "cpporm/builder_parts/conditions/where_mixin.h"

namespace cpporm {

    // This mixin now composes the more specific condition mixins
    // through multiple inheritance.
    template <typename Derived>
    class QueryBuilderConditionsMixin : public WhereMixin<Derived>, public OrMixin<Derived>, public NotMixin<Derived>, public InMixin<Derived> {
      protected:
        // Central state accessor, accessible by all sub-mixins via CRTP
        QueryBuilderState &_state() {
            return static_cast<Derived *>(this)->getState_();
        }
        const QueryBuilderState &_state() const {
            return static_cast<const Derived *>(this)->getState_();
        }

      public:
        // Accessors to the final condition lists from the state
        const std::vector<Condition> &getWhereConditions_mixin() const {
            return _state().where_conditions_;
        }
        const std::vector<Condition> &getOrConditions_mixin() const {
            return _state().or_conditions_;
        }
        const std::vector<Condition> &getNotConditions_mixin() const {
            return _state().not_conditions_;
        }
    };

}  // namespace cpporm

#endif  // cpporm_QUERY_BUILDER_CONDITIONS_MIXIN_H