#include "boltprotocol/message_defs.h"  // Includes all sub-headers like bolt_core_types.h, bolt_errors_versions.h

#include <algorithm>    // For std::visit in Value::operator==
#include <type_traits>  // For std::decay_t in Value::operator==

// Note: <arpa/inet.h> or <winsock2.h> for htonl/ntohl are now encapsulated in detail/byte_order_utils.h

namespace boltprotocol {

    // --- Definitions for constants and functions declared extern in headers ---

    // Definition for DEFAULT_USER_AGENT_FORMAT_STRING (declared in bolt_core_types.h, re-declared extern in message_defs.h)
    const std::string DEFAULT_USER_AGENT_FORMAT_STRING = "BoltCppDriver/0.3.0 (C++26; NoExcept)";  // Example version update

    // Definition for operator==(const Value&, const Value&) (declared in bolt_core_types.h, re-declared in message_defs.h)
    bool operator==(const Value& lhs, const Value& rhs) {
        if (lhs.index() != rhs.index()) {
            return false;
        }
        return std::visit(
            [&rhs](const auto& lhs_alternative_value) -> bool {
                using AlternativeType = std::decay_t<decltype(lhs_alternative_value)>;
                const AlternativeType& rhs_alternative_value = std::get<AlternativeType>(rhs);

                if constexpr (std::is_same_v<AlternativeType, std::shared_ptr<BoltList>> || std::is_same_v<AlternativeType, std::shared_ptr<BoltMap>> || std::is_same_v<AlternativeType, std::shared_ptr<PackStreamStructure>>) {
                    if (static_cast<bool>(lhs_alternative_value) != static_cast<bool>(rhs_alternative_value)) {
                        return false;
                    }
                    if (!lhs_alternative_value) {  // Both are null
                        return true;
                    }
                    return *lhs_alternative_value == *rhs_alternative_value;  // Compare pointed-to objects
                } else {
                    return lhs_alternative_value == rhs_alternative_value;
                }
            },
            lhs);
    }

    namespace versions {

        // --- Definitions for Version struct methods (declared in bolt_errors_versions.h) ---
        bool Version::operator<(const Version& other) const {
            if (major != other.major) {
                return major < other.major;
            }
            return minor < other.minor;
        }

        bool Version::operator==(const Version& other) const {
            return major == other.major && minor == other.minor;
        }

        bool Version::operator!=(const Version& other) const {
            return !(*this == other);
        }

        std::array<uint8_t, 4> Version::to_handshake_bytes() const {
            std::array<uint8_t, 4> bytes{};
            bytes[2] = major;
            bytes[3] = minor;
            return bytes;
        }

        BoltError Version::from_handshake_bytes(const std::array<uint8_t, 4>& bytes, Version& out_version) {
            if (bytes[0] == 0 && bytes[1] == 0) {
                out_version.major = bytes[2];
                out_version.minor = bytes[3];
                if (out_version.major == 0 && out_version.minor == 0) {  // All zeros
                    return BoltError::SUCCESS;                           // Represents "No Version" or successful parsing of 0.0
                }
                // Handle historic single-number versions if necessary, e.g., 0x00000001 -> 1.0
                if (out_version.major == 0 && out_version.minor == 1 && bytes[2] == 0) {  // Check if it was truly 0.1 from bytes or 1.0
                                                                                          // This specific check might need refinement based on how historic versions are encoded.
                                                                                          // The current to_handshake_bytes for 1.0 would be 00 00 01 00.
                                                                                          // If server sends 00 00 00 01 for 1.0, this logic needs adjustment.
                                                                                          // Assuming standard X.Y (00 00 Maj Min) or all zeros.
                }
                return BoltError::SUCCESS;
            }
            // Handle Bolt 4.3+ ranged versions if necessary, based on byte[0] and byte[1].
            // The spec says: "The first 8 bits are reserved. The next 8 bits represent the number of
            // consecutive minor versions below the specified minor...".
            // This implies byte[0] != 0 for ranged versions. For now, we only handle 00 00 Maj Min.
            // A full implementation would parse byte[1] as range length if byte[0] is non-zero (or specific pattern).
            return BoltError::UNSUPPORTED_PROTOCOL_VERSION;  // Or more specific error for unhandled format
        }

        // --- Definitions for extern version constants (declared in bolt_errors_versions.h) ---
        const Version V5_4(5, 4);
        const Version V5_3(5, 3);
        const Version V5_2(5, 2);
        const Version V5_1(5, 1);
        const Version V5_0(5, 0);
        const Version V4_4(4, 4);
        // const Version V4_3(4,3); // Define if declared extern
        // const Version V4_2(4,2);
        // const Version V4_1(4,1);
        // const Version V4_0(4,0);
        // const Version V3_0(3,0);

        // --- Definition for get_default_proposed_versions (declared in bolt_errors_versions.h) ---
        // Static to ensure it's initialized once.
        static const std::vector<Version> DEFAULT_PROPOSED_VERSIONS_LIST = {
            V5_4, V5_3, V5_2, V5_1, V5_0, V4_4  // Order from newest to oldest preferred
            // Add V4_3, V4_2 etc. if they are defined constants and desired in default list
        };

        const std::vector<Version>& get_default_proposed_versions() {
            return DEFAULT_PROPOSED_VERSIONS_LIST;
        }

    }  // namespace versions

}  // namespace boltprotocol