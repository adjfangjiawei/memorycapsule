#ifndef BOLTPROTOCOL_CORE_TYPES_H
#define BOLTPROTOCOL_CORE_TYPES_H

#include <cstdint>  // For uintXX_t types
#include <map>
#include <memory>  // For std::shared_ptr
#include <string>
#include <variant>  // For std::variant
#include <vector>

// Forward declarations within this file if mutually dependent, or include necessary headers
// For now, PackStreamStructure, BoltList, BoltMap are defined before Value uses them with shared_ptr.

namespace boltprotocol {

    // Forward declarations for Value variant members
    struct BoltList;
    struct BoltMap;
    struct PackStreamStructure;

    // Core PackStream Value type
    using Value = std::variant<std::nullptr_t, bool, int64_t, double, std::string, std::shared_ptr<BoltList>, std::shared_ptr<BoltMap>, std::shared_ptr<PackStreamStructure> >;

    // Definition for BoltList
    struct BoltList {
        std::vector<Value> elements;
        bool operator==(const BoltList& other) const {
            // Simple comparison, might need deep comparison for shared_ptr<Value> elements if Value itself can be complex.
            // Current Value::operator== handles shared_ptr comparison correctly.
            return elements == other.elements;
        }
    };

    // Definition for BoltMap
    struct BoltMap {
        std::map<std::string, Value> pairs;
        bool operator==(const BoltMap& other) const {
            return pairs == other.pairs;
        }
    };

    // Definition for PackStreamStructure
    struct PackStreamStructure {
        uint8_t tag = 0;
        std::vector<Value> fields;
        bool operator==(const PackStreamStructure& other) const {
            return tag == other.tag && fields == other.fields;
        }
    };

    // Global operator== for Value, needs full definitions of BoltList, BoltMap, PackStreamStructure
    // This declaration should ideally be where Value is fully defined or usable.
    // If moved to a .cpp, it needs to be declared here.
    // Keeping it here for header-only convenience if types are simple enough.
    // bool operator==(const Value& lhs, const Value& rhs); // Definition will be in message_defs.cpp (or a new core_types.cpp)

    // Global Constants
    constexpr uint32_t BOLT_MAGIC_PREAMBLE = 0x6060B017;
    // extern const std::string DEFAULT_USER_AGENT_FORMAT_STRING; // Declaration here, definition in a .cpp file
    constexpr uint16_t MAX_CHUNK_PAYLOAD_SIZE = 65535;
    constexpr uint16_t CHUNK_HEADER_SIZE = 2;

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_CORE_TYPES_H