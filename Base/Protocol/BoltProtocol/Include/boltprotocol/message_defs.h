#ifndef BOLTPROTOCOL_MESSAGE_DEFS_H
#define BOLTPROTOCOL_MESSAGE_DEFS_H

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace boltprotocol {

    // Forward declarations
    struct BoltList;
    struct BoltMap;
    struct PackStreamStructure;

    using Value = std::variant<std::nullptr_t, bool, int64_t, double, std::string, std::shared_ptr<BoltList>, std::shared_ptr<BoltMap>, std::shared_ptr<PackStreamStructure>>;

    struct BoltList {
        std::vector<Value> elements;
        bool operator==(const BoltList& other) const {
            return elements == other.elements;
        }
    };

    struct BoltMap {
        std::map<std::string, Value> pairs;
        bool operator==(const BoltMap& other) const {
            return pairs == other.pairs;
        }
    };

    struct PackStreamStructure {
        uint8_t tag = 0;
        std::vector<Value> fields;
        bool operator==(const PackStreamStructure& other) const {
            return tag == other.tag && fields == other.fields;
        }
    };

    bool operator==(const Value& lhs, const Value& rhs);

    constexpr uint32_t BOLT_MAGIC_PREAMBLE = 0x6060B017;
    extern const std::string DEFAULT_USER_AGENT_FORMAT_STRING;
    constexpr uint16_t MAX_CHUNK_PAYLOAD_SIZE = 65535;
    constexpr uint16_t CHUNK_HEADER_SIZE = 2;

    enum class BoltError {
        SUCCESS = 0,
        UNKNOWN_ERROR,
        INVALID_ARGUMENT,
        SERIALIZATION_ERROR,
        DESERIALIZATION_ERROR,
        INVALID_MESSAGE_FORMAT,
        UNSUPPORTED_PROTOCOL_VERSION,
        NETWORK_ERROR,
        HANDSHAKE_FAILED,
        HANDSHAKE_NO_COMMON_VERSION,
        HANDSHAKE_MAGIC_MISMATCH,
        CHUNK_TOO_LARGE,
        CHUNK_ENCODING_ERROR,
        CHUNK_DECODING_ERROR,
        OUT_OF_MEMORY,
        RECURSION_DEPTH_EXCEEDED,
        MESSAGE_TOO_LARGE
    };

    namespace versions {
        struct Version {
            uint8_t major = 0;
            uint8_t minor = 0;
            Version() = default;
            constexpr Version(uint8_t maj, uint8_t min) : major(maj), minor(min) {
            }
            bool operator<(const Version& other) const;
            bool operator==(const Version& other) const;
            bool operator!=(const Version& other) const;
            std::array<uint8_t, 4> to_handshake_bytes() const;
            static BoltError from_handshake_bytes(const std::array<uint8_t, 4>& bytes, Version& out_version);
        };
        extern const Version V5_4, V5_3, V5_2, V5_1, V5_0, V4_4;
        extern const std::vector<Version>& get_default_proposed_versions();
    }  // namespace versions

    enum class MessageTag : uint8_t {
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
        SUCCESS = 0x70,
        RECORD = 0x71,
        IGNORED = 0x7E,
        FAILURE = 0x7F,
    };

    // --- Message Parameter Structures ---
    struct HelloMessageParams {
        std::map<std::string, Value> extra_auth_tokens;
    };
    struct RunMessageParams {
        std::string cypher_query;
        std::map<std::string, Value> parameters;
        std::map<std::string, Value> extra_metadata;
    };
    struct DiscardMessageParams {
        std::optional<int64_t> n;
        std::optional<int64_t> qid;
    };
    struct PullMessageParams {
        std::optional<int64_t> n;
        std::optional<int64_t> qid;
    };

    struct BeginMessageParams {
        std::map<std::string, Value> extra;
    };
    struct CommitMessageParams { /* No parameters */
    };
    struct RollbackMessageParams { /* No parameters */
    };

    struct RouteMessageParams {
        std::map<std::string, Value> routing_context;
        std::vector<std::string> bookmarks;
        std::optional<std::string> db_name;
        std::optional<std::string> impersonated_user;
    };

    struct SuccessMessageParams {
        std::map<std::string, Value> metadata;
    };
    struct RecordMessageParams {
        std::vector<Value> fields;
    };
    struct FailureMessageParams {
        std::map<std::string, Value> metadata;
    };

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_MESSAGE_DEFS_H