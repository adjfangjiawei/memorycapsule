// cpporm/session_fwd.h
#ifndef cpporm_SESSION_FWD_H
#define cpporm_SESSION_FWD_H

#include <memory> // For std::unique_ptr if used in forward declared params/returns

namespace cpporm {

class Session; // Forward declaration
class QueryBuilder;
struct OnConflictClause; // Forward declaration if needed by SessionCore setters
                         // but defined elsewhere (e.g., query_builder_state.h)

// If SessionOnConflictUpdateSetter is tightly coupled and only used by Session,
// its forward declaration or full definition might go here or in session_core.h
// For now, let's assume it's defined before session_core.h needs its full type.

} // namespace cpporm

#endif // cpporm_SESSION_FWD_H