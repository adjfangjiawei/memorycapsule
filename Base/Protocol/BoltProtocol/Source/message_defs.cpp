#include "boltprotocol/message_defs.h"

#include <algorithm>    // For std::visit in Value::operator==
#include <cstring>      // For std::memcpy
#include <functional>   // For std::visit (though variant includes it)
#include <type_traits>  // For std::decay_t

#include "boltprotocol/detail/byte_order_utils.h"  // For host_to_be, be_to_host

// Note: <arpa/inet.h> or <winsock2.h> for htonl/ntohl are now encapsulated in byte_order_utils.h
// and used by detail::host_to_be and detail::be_to_host.

namespace boltprotocol {

    // --- Value Equality Implementation ---
    bool operator==(const Value& lhs, const Value& rhs) {
        if (lhs.index() != rhs.index()) {
            return false;
        }
        // std::visit is a good way to compare variants.
        // It calls the lambda with the alternatives held by lhs.
        return std::visit(
            [&rhs](const auto& lhs_alternative_value) -> bool {
                // Get the same type from rhs. This is safe because index() matched.
                using AlternativeType = std::decay_t<decltype(lhs_alternative_value)>;
                const AlternativeType& rhs_alternative_value = std::get<AlternativeType>(rhs);

                // Handle shared_ptr cases specifically: compare pointed-to values if both non-null.
                if constexpr (std::is_same_v<AlternativeType, std::shared_ptr<BoltList>> || std::is_same_v<AlternativeType, std::shared_ptr<BoltMap>> || std::is_same_v<AlternativeType, std::shared_ptr<PackStreamStructure>>) {
                    // Both null: equal. One null, other not: not equal.
                    if (static_cast<bool>(lhs_alternative_value) != static_cast<bool>(rhs_alternative_value)) {
                        return false;
                    }
                    if (!lhs_alternative_value) {  // Both are null
                        return true;
                    }
                    // Both are non-null, compare the pointed-to objects.
                    return *lhs_alternative_value == *rhs_alternative_value;
                } else {
                    // For non-shared_ptr types (nullptr_t, bool, int64_t, double, string)
                    return lhs_alternative_value == rhs_alternative_value;
                }
            },
            lhs);
    }

    // --- Global Definitions ---
    const std::string DEFAULT_USER_AGENT_FORMAT_STRING = "BoltCppDriver/0.2.0 (C++26; NoExcept)";  // Example version

    // --- versions::Version Implementation ---
    namespace versions {

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

        // Returns the 4-byte big-endian representation for handshake
        // E.g., Bolt 5.4 is represented as 00 00 05 04 in Big Endian
        std::array<uint8_t, 4> Version::to_handshake_bytes() const {
            std::array<uint8_t, 4> bytes{};  // Value-initialize to all zeros
            // Bolt versions X.Y are represented as 00 00 0X 0Y (Big Endian)
            // This means byte[0]=0, byte[1]=0, byte[2]=X, byte[3]=Y.
            // This is already in "network byte order" if we construct it this way.
            if (major == 0 && minor == 0) {
                // All zeros is fine, represents "No Version" or an uninitialized version.
            } else {
                bytes[2] = major;
                bytes[3] = minor;
            }
            return bytes;
        }

        // Creates a version from the 4-byte big-endian handshake representation.
        BoltError Version::from_handshake_bytes(const std::array<uint8_t, 4>& bytes, Version& out_version) {
            // Bytes are expected in Big Endian: byte[0], byte[1], byte[2] (Major), byte[3] (Minor)
            // Example: 0x00, 0x00, 0x05, 0x04 for Bolt 5.4

            // A server might respond with a single number for very old versions,
            // e.g., 0x00000001 for Bolt 1.0.
            // The Bolt specification typically shows X.Y as 00 00 0X 0Y.

            if (bytes[0] == 0 && bytes[1] == 0) {
                if (bytes[2] == 0 && bytes[3] == 0) {  // All zeros: 0.0 ("No Version" agreed upon)
                    out_version.major = 0;
                    out_version.minor = 0;
                    return BoltError::SUCCESS;  // Or HANDSHAKE_NO_COMMON_VERSION if this is server response
                }
                // Handle 00 00 0X 0Y format
                out_version.major = bytes[2];
                out_version.minor = bytes[3];
                // Basic validation: major version should not be 0 if minor is non-zero,
                // unless it's a special single-number version.
                if (out_version.major == 0 && out_version.minor != 0) {
                    // Check for older single-number versions if needed, e.g. 0x00000001 -> 1.0
                    if (out_version.minor == 1) {  // 0x00000001
                        out_version.major = 1;
                        out_version.minor = 0;
                        return BoltError::SUCCESS;
                    }
                    // Add other historic versions if necessary
                    // For now, consider other 0.Y as invalid for handshake response.
                    return BoltError::UNSUPPORTED_PROTOCOL_VERSION;  // Or INVALID_MESSAGE_FORMAT
                }
                return BoltError::SUCCESS;
            } else {
                // If bytes[0] or bytes[1] are non-zero, it's not the standard X.Y format
                // or the all-zero "No Version". This is unexpected for modern Bolt.
                return BoltError::UNSUPPORTED_PROTOCOL_VERSION;  // Or INVALID_MESSAGE_FORMAT
            }
        }

        // --- Predefined Version Constants ---
        const Version V5_4(5, 4);
        const Version V5_3(5, 3);
        const Version V5_2(5, 2);
        const Version V5_1(5, 1);
        const Version V5_0(5, 0);
        const Version V4_4(4, 4);
        // Add V4_3, V4_2 etc. if they need to be common constants
        // const Version V4_3(4,3);
        // const Version V4_2(4,2);

        // Provides a default-ordered list of versions a client might propose.
        // Static to ensure it's initialized once.
        static const std::vector<Version> DEFAULT_PROPOSED_VERSIONS_LIST = {
            V5_4, V5_3, V5_2, V5_1, V5_0, V4_4  // Order from newest to oldest preferred
        };

        const std::vector<Version>& get_default_proposed_versions() {
            return DEFAULT_PROPOSED_VERSIONS_LIST;
        }

    }  // namespace versions

}  // namespace boltprotocol