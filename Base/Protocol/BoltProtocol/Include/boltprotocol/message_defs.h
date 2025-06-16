#ifndef BOLTPROTOCOL_MESSAGE_DEFS_H
#define BOLTPROTOCOL_MESSAGE_DEFS_H

// This file now serves as an aggregate header for convenience.
// For more granular includes, users can include the specific headers directly.

#include "bolt_core_types.h"
#include "bolt_errors_versions.h"
#include "bolt_message_params.h"
#include "bolt_message_tags.h"

// The extern const string DEFAULT_USER_AGENT_FORMAT_STRING needs a definition in a .cpp file.
// The operator==(const Value&, const Value&) also needs a definition.
// These will be placed in message_defs.cpp (which now mainly serves to provide these definitions).

namespace boltprotocol {
    // Definition of DEFAULT_USER_AGENT_FORMAT_STRING is in message_defs.cpp
    extern const std::string DEFAULT_USER_AGENT_FORMAT_STRING;

    // Definition of operator==(Value, Value) is in message_defs.cpp
    bool operator==(const Value& lhs, const Value& rhs);
}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_MESSAGE_DEFS_H