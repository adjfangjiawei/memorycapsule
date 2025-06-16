#ifndef cpporm_QUERY_BUILDER_PRELOAD_MIXIN_H
#define cpporm_QUERY_BUILDER_PRELOAD_MIXIN_H

#include "cpporm/builder_parts/query_builder_state.h" // For QueryBuilderState, PreloadRequest
#include <string>
#include <vector> // For state_.preload_requests_

namespace cpporm {

template <typename Derived> class QueryBuilderPreloadMixin {
protected:
  QueryBuilderState &_state() {
    return static_cast<Derived *>(this)->getState_();
  }
  const QueryBuilderState &_state() const {
    return static_cast<const Derived *>(this)->getState_();
  }

public:
  // Simple Preload by association C++ field name.
  // GORM also supports Preload("Orders.OrderItems") for nested preloading,
  // and Preload("Orders", func(db *gorm.DB) *gorm.DB { ... }) for conditional
  // preloading. We'll start with the basic form.
  Derived &Preload(const std::string &association_cpp_field_name) {
    // TODO: Add support for dot-separated nested preload paths if needed.
    // For now, assume association_cpp_field_name is a direct association of the
    // current model.
    _state().preload_requests_.emplace_back(association_cpp_field_name);
    return static_cast<Derived &>(*this);
  }

  // Accessor for preload requests (mainly for Session to use)
  const std::vector<PreloadRequest> &getPreloadRequests_mixin() const {
    return _state().preload_requests_;
  }
};

} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_PRELOAD_MIXIN_H