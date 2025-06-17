#ifndef NEO4J_BOLT_DRIVER_BOLT_PROTOCOL_H
#define NEO4J_BOLT_DRIVER_BOLT_PROTOCOL_H

#include <array>
#include <cstdint>
#include <vector>

namespace neo4j_bolt_driver {
    namespace protocol {

        // Magic Preamble for Bolt connection handshake
        constexpr uint32_t BOLT_MAGIC_PREAMBLE = 0x6060B017;

        namespace versions {
            // Bytes sent for a single version in the proposal list: {Major, Minor, Patch,
            // Revision}
            const extern std::array<uint8_t, 4> V5_4;  // Neo4j 5.0 supports Bolt 5.0-5.4
            const extern std::array<uint8_t, 4> V5_3;
            const extern std::array<uint8_t, 4> V5_2;
            const extern std::array<uint8_t, 4> V5_1;
            const extern std::array<uint8_t, 4> V5_0;
            const extern std::array<uint8_t, 4> V4_4;  // Neo4j 4.4 supports Bolt 4.2-4.4
            const extern std::array<uint8_t, 4> V4_3;
            const extern std::array<uint8_t, 4> V4_2;

            // Default proposed versions in preferred order
            // The actual vector will be defined in a .cpp file
            extern const std::vector<std::array<uint8_t, 4>>& get_default_proposed_versions();

        }  // namespace versions

        enum class MessageTag : uint8_t {
            // Requests
            HELLO = 0x01,
            GOODBYE = 0x02,
            RESET = 0x0F,

            RUN = 0x10,
            DISCARD = 0x2F,
            PULL = 0x3F,

            BEGIN = 0x11,
            COMMIT = 0x12,
            ROLLBACK = 0x13,

            ROUTE = 0x66,

            // Responses
            SUCCESS = 0x70,
            RECORD = 0x71,
            IGNORED = 0x7E,
            FAILURE = 0x7F
        };

        constexpr uint16_t MAX_CHUNK_SIZE = 65535;

    }  // namespace protocol
}  // namespace neo4j_bolt_driver

#endif  // NEO4J_BOLT_DRIVER_BOLT_PROTOCOL_H