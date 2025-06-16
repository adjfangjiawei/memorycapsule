#ifndef BOLTPROTOCOL_MESSAGE_DEFS_H
#define BOLTPROTOCOL_MESSAGE_DEFS_H

#include "bolt_core_types.h"
#include "bolt_errors_versions.h"
#include "bolt_message_params.h"
#include "bolt_message_tags.h"
#include "bolt_structure_types.h"  // <--- ADDED: For BoltNode, BoltDate etc.
// bolt_structure_serialization.h is for functions, not direct types needed by message_params generally

namespace boltprotocol {
    extern const std::string DEFAULT_USER_AGENT_FORMAT_STRING;
    bool operator==(const Value& lhs, const Value& rhs);
}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_MESSAGE_DEFS_H