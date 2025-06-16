#ifndef BOLTPROTOCOL_MESSAGE_DEFS_H
#define BOLTPROTOCOL_MESSAGE_DEFS_H

#include <array>
#include <cstdint>
#include <map>
#include <memory>  // For std::shared_ptr
#include <optional>
#include <string>
#include <type_traits>  // For std::decay_t in operator== (if needed, or in .cpp)
#include <variant>
#include <vector>

namespace boltprotocol {

    // Forward declarations for types that will be wrapped by shared_ptr in the Value variant
    struct BoltList;
    struct BoltMap;
    struct PackStreamStructure;

    // The main Value type alias for PackStream values
    using Value = std::variant<std::nullptr_t,                       // Null
                               bool,                                 // Boolean
                               int64_t,                              // Integer
                               double,                               // Float
                               std::string,                          // String
                               std::shared_ptr<BoltList>,            // List (using shared_ptr)
                               std::shared_ptr<BoltMap>,             // Map (using shared_ptr)
                               std::shared_ptr<PackStreamStructure>  // Structure (using shared_ptr)
                               >;

    // Definitions of the helper structs that are pointed to by shared_ptr.
    // These structs can use 'Value' because 'Value' (the variant alias) is now a complete type.

    struct BoltList {
        std::vector<Value> elements;

        bool operator==(const BoltList& other) const {
            return elements == other.elements;
        }
        // Add operator< if BoltList needs to be comparable for ordered collections
    };

    struct BoltMap {
        std::map<std::string, Value> pairs;

        bool operator==(const BoltMap& other) const {
            return pairs == other.pairs;
        }
        // Add operator< if BoltMap needs to be comparable
    };

    struct PackStreamStructure {
        uint8_t tag;
        std::vector<Value> fields;

        bool operator==(const PackStreamStructure& other) const {
            return tag == other.tag && fields == other.fields;
        }
        // Add operator< if PackStreamStructure needs to be comparable
    };

    // Equality operator for the Value variant itself
    // Declaration - implementation will be in the .cpp file
    bool operator==(const Value& lhs, const Value& rhs);
    // bool operator!=(const Value& lhs, const Value& rhs); // Can be defaulted or implemented via ==
    // bool operator<(const Value& lhs, const Value& rhs); // If needed for ordered collections of Values

    // Magic Preamble for Bolt connection handshake
    constexpr uint32_t BOLT_MAGIC_PREAMBLE = 0x6060B017;  // Network Byte Order (Big Endian)

    // Default User Agent string template for HELLO message
    extern const std::string DEFAULT_USER_AGENT_FORMAT_STRING;

    enum class BoltError { SUCCESS = 0, UNKNOWN_ERROR, INVALID_ARGUMENT, SERIALIZATION_ERROR, DESERIALIZATION_ERROR, INVALID_MESSAGE_FORMAT, UNSUPPORTED_PROTOCOL_VERSION, NETWORK_ERROR, HANDSHAKE_FAILED, CHUNK_TOO_LARGE, OUT_OF_MEMORY, RECURSION_DEPTH_EXCEEDED, HANDSHAKE_NO_COMMON_VERSION };

    namespace versions {
        struct Version {
            uint8_t major;
            uint8_t minor;

            bool operator<(const Version& other) const;
            bool operator==(const Version& other) const;
            bool operator!=(const Version& other) const;

            uint32_t to_handshake_int() const;
            std::array<uint8_t, 4> to_handshake_bytes() const;
        };

        extern const Version V5_4;
        extern const Version V5_3;
        extern const Version V5_2;
        extern const Version V5_1;
        extern const Version V5_0;
        extern const Version V4_4;
        extern const Version V4_3;
        extern const Version V4_2;

        extern const std::vector<Version>& get_default_proposed_versions();
    }  // namespace versions

    enum class MessageTag : uint8_t {
        // Client -> Server Messages
        HELLO = 0x01,
        RUN = 0x10,
        DISCARD = 0x2F,
        PULL = 0x3F,
        BEGIN = 0x11,
        COMMIT = 0x12,
        ROLLBACK = 0x13,
        RESET = 0x0F,
        GOODBYE = 0x02,
        ROUTE = 0x66,
        TELEMETRY = 0x54,

        // Server -> Client Messages
        SUCCESS = 0x70,
        RECORD = 0x71,
        IGNORED = 0x7E,
        FAILURE = 0x7F,
    };

    constexpr uint16_t MAX_CHUNK_PAYLOAD_SIZE = 65535;
    constexpr uint16_t CHUNK_HEADER_SIZE = 2;

    struct HelloMessageParams {
        // This map contains all key-value pairs for the HELLO message metadata.
        // Keys like "user_agent", "scheme", "principal", "credentials", "routing", "db", "bolt_agent", etc.
        // The values are `boltprotocol::Value` type.
        std::map<std::string, Value> extra_auth_tokens;
    };

    struct RunMessageParams {
        std::string cypher_query;
        std::map<std::string, Value> parameters;
        std::map<std::string, Value> extra_metadata;  // bookmarks, tx_timeout, tx_metadata, mode, db, imp_user
    };

    struct DiscardMessageParams {
        std::optional<int64_t> n;    // Number of records to discard, -1 for all
        std::optional<int64_t> qid;  // Query ID
    };

    struct PullMessageParams {
        std::optional<int64_t> n;    // Number of records to pull, -1 for all
        std::optional<int64_t> qid;  // Query ID
    };

    struct SuccessMessageParams {
        std::map<std::string, Value> metadata;
    };

    struct RecordMessageParams {
        std::vector<Value> fields;
    };

    struct FailureMessageParams {
        std::map<std::string, Value> metadata;  // "code" and "message"
    };

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_MESSAGE_DEFS_H