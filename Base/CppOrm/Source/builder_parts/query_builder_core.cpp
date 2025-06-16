// cpporm/builder_parts/query_builder_core.cpp
#include "cpporm/query_builder_core.h"
#include "cpporm/i_query_executor.h"
#include "cpporm/model_base.h"
#include "cpporm/session.h" // For Session::anyToQueryValueForSessionConvenience

#include <QDebug>
#include <QMetaType>
#include <sstream>
#include <variant>

namespace cpporm {

// Non-template member function definitions for QueryBuilder have been moved to:
// - query_builder_lifecycle.cpp (constructors, destructor, assignments)
// - query_builder_setters_core.cpp (Model, Table, From, With, OnConflict,
// SelectSubquery, QB-specific Where/Or/Not)
// - query_builder_execution_non_template.cpp (First(ModelBase&), Find, Create,
// Updates, Delete, Save, Count)
// - query_builder_utils.cpp (getFromSourceName, AsSubquery, static
// quoteSqlIdentifier, static toQVariant, toSqlDebug)
// - query_builder_helpers.cpp (internal SQL build helpers)

// This file (query_builder_core.cpp) is now primarily for any potential future
// non-template, non-static QueryBuilder methods that don't fit into the above
// categories, or if some specific core logic needed a central, non-header
// definition point. Currently, based on the linker errors, it seems all
// problematic definitions were duplicated from their intended separate files.

} // namespace cpporm