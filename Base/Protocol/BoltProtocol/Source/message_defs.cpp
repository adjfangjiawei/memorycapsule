#include "boltprotocol/message_defs.h"

#include <algorithm>
#include <cstring>
#include <functional>   // For std::visit (though variant includes it)
#include <type_traits>  // For std::decay_t

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>  // For ntohl, htonl
#endif

namespace boltprotocol {

    // Implementation for Value's equality operator
    bool operator==(const Value& lhs, const Value& rhs) {
        if (lhs.index() != rhs.index()) {
            return false;
        }

        return std::visit(
            [&rhs](const auto& lhs_val_variant_alternative) -> bool {
                // Get the same type from rhs. This is safe because index() matched.
                using T_alt = std::decay_t<decltype(lhs_val_variant_alternative)>;
                const T_alt& rhs_val_variant_alternative = std::get<T_alt>(rhs);

                // Handle shared_ptr cases specifically: compare pointed-to values
                if constexpr (std::is_same_v<T_alt, std::shared_ptr<BoltList>>) {
                    // Both null: equal. One null, other not: not equal. Both non-null: compare contents.
                    if (static_cast<bool>(lhs_val_variant_alternative) != static_cast<bool>(rhs_val_variant_alternative)) return false;
                    if (!lhs_val_variant_alternative) return true;  // Both are null
                    return *lhs_val_variant_alternative == *rhs_val_variant_alternative;
                } else if constexpr (std::is_same_v<T_alt, std::shared_ptr<BoltMap>>) {
                    if (static_cast<bool>(lhs_val_variant_alternative) != static_cast<bool>(rhs_val_variant_alternative)) return false;
                    if (!lhs_val_variant_alternative) return true;
                    return *lhs_val_variant_alternative == *rhs_val_variant_alternative;
                } else if constexpr (std::is_same_v<T_alt, std::shared_ptr<PackStreamStructure>>) {
                    if (static_cast<bool>(lhs_val_variant_alternative) != static_cast<bool>(rhs_val_variant_alternative)) return false;
                    if (!lhs_val_variant_alternative) return true;
                    return *lhs_val_variant_alternative == *rhs_val_variant_alternative;
                } else {
                    // For non-shared_ptr types (nullptr_t, bool, int64_t, double, string)
                    return lhs_val_variant_alternative == rhs_val_variant_alternative;
                }
            },
            lhs);
    }

    const std::string DEFAULT_USER_AGENT_FORMAT_STRING = "MyCppBoltDriver/0.1.0";  // Example version

    namespace versions {

        // Helper to convert uint32_t to big-endian byte array
        std::array<uint8_t, 4> u32_to_be_bytes(uint32_t val) {
            std::array<uint8_t, 4> bytes;
            uint32_t be_val = htonl(val);  // Convert host to network byte order (big endian)
            std::memcpy(bytes.data(), &be_val, sizeof(uint32_t));
            return bytes;
        }

        uint32_t Version::to_handshake_int() const {
            return static_cast<uint32_t>((static_cast<uint32_t>(minor) << 8) | static_cast<uint32_t>(major));
        }

        std::array<uint8_t, 4> Version::to_handshake_bytes() const {
            uint32_t val = to_handshake_int();
            return u32_to_be_bytes(val);
        }

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

        const Version V5_4 = {5, 4};
        const Version V5_3 = {5, 3};
        const Version V5_2 = {5, 2};
        const Version V5_1 = {5, 1};
        const Version V5_0 = {5, 0};
        const Version V4_4 = {4, 4};
        const Version V4_3 = {4, 3};
        const Version V4_2 = {4, 2};

        static const std::vector<Version> DEFAULT_PROPOSED_VERSIONS = {V5_4, V5_3, V5_2, V5_1, V5_0, V4_4, V4_3, V4_2};

        const std::vector<Version>& get_default_proposed_versions() {
            return DEFAULT_PROPOSED_VERSIONS;
        }

    }  // namespace versions

}  // namespace boltprotocol