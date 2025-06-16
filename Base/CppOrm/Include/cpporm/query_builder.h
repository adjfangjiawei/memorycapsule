// cpporm/query_builder.h
#ifndef cpporm_QUERY_BUILDER_H
#define cpporm_QUERY_BUILDER_H

// Core QueryBuilder class definition (non-template members, mixins, state)
#include "cpporm/query_builder_core.h"

// Template member implementations for QueryBuilder (e.g., First<T>, Find<T>)
#include "cpporm/query_builder_execution.h"

// query_builder_setters.h is currently minimal as setters are part of
// QueryBuilder class #include "cpporm/query_builder_setters.h"

// query_builder_fwd.h is for forward declarations, not typically included by
// the main header itself, but by files that need to forward declare
// QueryBuilder.

#endif // cpporm_QUERY_BUILDER_H