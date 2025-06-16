#include "neo4j_bolt_driver/bolt_protocol.h"

namespace neo4j_bolt_driver {
    namespace protocol {
        namespace versions {

            // Definitions for Bolt Protocol Versions
            // {Major, Minor, Patch, Revision}
            const std::array<uint8_t, 4> V5_4 = {0x05, 0x04, 0x00, 0x00};
            const std::array<uint8_t, 4> V5_3 = {0x05, 0x03, 0x00, 0x00};
            const std::array<uint8_t, 4> V5_2 = {0x05, 0x02, 0x00, 0x00};
            const std::array<uint8_t, 4> V5_1 = {0x05, 0x01, 0x00, 0x00};
            const std::array<uint8_t, 4> V5_0 = {0x05, 0x00, 0x00, 0x00};
            const std::array<uint8_t, 4> V4_4 = {0x04, 0x04, 0x00, 0x00};
            const std::array<uint8_t, 4> V4_3 = {0x04, 0x03, 0x00, 0x00};
            const std::array<uint8_t, 4> V4_2 = {0x04, 0x02, 0x00, 0x00};

            // Definition for the default proposed versions vector
            // This is static to ensure it's initialized only once.
            const std::vector<std::array<uint8_t, 4>>& get_default_proposed_versions() {
                static const std::vector<std::array<uint8_t, 4>> default_versions = {
                    // Most recent preferred versions first
                    V5_4,
                    V5_3,
                    V5_2,
                    V5_1,
                    V5_0,
                    V4_4,
                    V4_3,
                    V4_2
                    // Add older versions if wider compatibility is absolutely necessary,
                    // but generally, focusing on recent stable versions is better.
                };
                return default_versions;
            }

        }  // namespace versions
    }  // namespace protocol
}  // namespace neo4j_bolt_driver