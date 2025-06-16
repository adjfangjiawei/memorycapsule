// cpporm/query_builder_setters.h
#ifndef cpporm_QUERY_BUILDER_SETTERS_H
#define cpporm_QUERY_BUILDER_SETTERS_H

// This file is intended for QueryBuilder setter method declarations if they
// were separated from the main QueryBuilder class definition, or for related
// free functions. Currently, setter methods like OnConflict..., With...,
// SelectSubquery... are declared directly within the QueryBuilder class in
// query_builder_core.h, and their non-template implementations are in
// query_builder_core.cpp.

// #include "cpporm/query_builder_fwd.h" // For forward declarations if needed
// #include "cpporm/builder_parts/query_builder_state.h" // For types like
// OnConflictClause

namespace cpporm {

// Declarations for OnConflictUpdateSetter are in query_builder_core.h as it's
// tightly coupled.

// If there were free functions related to QueryBuilder state manipulation or
// specific setters, they could be declared here. For now, this file remains
// mostly a placeholder.

} // namespace cpporm

#endif // cpporm_QUERY_BUILDER_SETTERS_H