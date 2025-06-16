#ifndef cpporm_QUERY_BUILDER_SCOPES_MIXIN_H
#define cpporm_QUERY_BUILDER_SCOPES_MIXIN_H

#include "cpporm/builder_parts/query_builder_state.h" // For QueryBuilderState

namespace cpporm {

template <typename Derived> class QueryBuilderScopesMixin {
protected:
  QueryBuilderState &_state() {
    return static_cast<Derived *>(this)->getState_();
  }
  const QueryBuilderState &_state() const {
    return static_cast<const Derived *>(this)->getState_();
  }

public:
  Derived &Unscoped() {
    _state().apply_soft_delete_scope_ = false;
    // Potentially disable other default scopes here if they are added
    return static_cast<Derived &>(*this);
  }

  bool isSoftDeleteScopeActive_mixin() const {
    return _state().apply_soft_delete_scope_;
  }
};

} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_SCOPES_MIXIN_H