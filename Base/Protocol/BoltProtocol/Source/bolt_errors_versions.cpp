// Base/Protocol/BoltProtocol/Source/bolt_errors_versions.cpp
// 或者这个内容应该在您原有的 Source/message_defs.cpp 中，如果是，请恢复它

#include "boltprotocol/bolt_errors_versions.h"  // 包含 Version 结构和常量声明

#include <algorithm>  // For std::min (if needed, though not in current Version funcs)
#include <cstring>    // For std::memcpy in from_handshake_bytes (original version had it)
#include <stdexcept>  // For std::to_string in Version::to_string()
#include <string>
#include <vector>

#include "boltprotocol/detail/byte_order_utils.h"  // 为了 to_handshake_bytes 和 from_handshake_bytes

namespace boltprotocol {
    namespace versions {

        // --- Definitions for Version struct methods ---
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

        bool Version::operator>(const Version& other) const {
            return other < *this;
        }

        bool Version::operator<=(const Version& other) const {
            return !(*this > other);
        }

        bool Version::operator>=(const Version& other) const {
            return !(*this < other);
        }

        std::string Version::to_string() const {
            return std::to_string(static_cast<int>(major)) + "." + std::to_string(static_cast<int>(minor));
        }

        std::array<uint8_t, 4> Version::to_handshake_bytes() const {
            // Per Bolt spec for handshake version slots (e.g., Bolt 4.0+):
            // Versions are 32-bit unsigned integers, big-endian.
            // Example: 5.4 is 0x00000504.
            // Your previous provided code for handshake.cpp used a similar construction:
            // uint32_t version_int32_for_handshake = (static_cast<uint32_t>(proposed_versions[i].major) << 8) | (static_cast<uint32_t>(proposed_versions[i].minor));
            // uint32_t version_be = detail::host_to_be(version_int32_for_handshake);
            // std::memcpy(out_handshake_bytes.data() + current_offset, &version_be, HANDSHAKE_VERSION_SIZE_BYTES);
            // However, the example handshake code `server_response_b = server_chosen_version_sim.to_handshake_bytes();`
            // and `Version::to_handshake_bytes()` in `message_defs.cpp` (from previous full listing) used `{0, 0, minor, major}`.
            // The spec for *server response* says: "the response will contain that version encoded as a single 32-bit integer."
            // The client *proposal slots* also take 32-bit big-endian integers.
            // Let's assume the intent for Version::to_handshake_bytes is to produce the 4-byte representation of THIS version
            // as it would appear in a handshake slot or response.
            // The common interpretation is [0,0,Major,Minor] if thinking about it byte-wise to form 0x0000MMNN (big-endian)
            // or if using your earlier message_defs.cpp code: {0,0,minor,major} which means MM=minor, NN=major.
            // Let's stick to the [0,0,Major,Minor] that maps to a simple u32.
            std::array<uint8_t, 4> bytes = {0, 0, 0, 0};
            uint32_t version_val = (static_cast<uint32_t>(major) << 8) | static_cast<uint32_t>(minor);
            uint32_t version_val_be = detail::host_to_be(version_val);
            std::memcpy(bytes.data(), &version_val_be, sizeof(uint32_t));
            return bytes;  // This will be [0,0,major,minor] if major/minor are single bytes and host is little-endian
                           // or [major,minor,0,0] if host is big-endian and we want 0xMMNN0000.
                           // Re-evaluating: The spec example "00 00 00 01" means version 1.0 (major=1, minor=0).
                           // So the 32-bit int is 0x0000MMNN where MM=major, NN=minor.
                           // Therefore, this implementation is correct if `version_val` is structured as Major in MSB of the relevant part.
                           // `(static_cast<uint32_t>(major) << 8) | static_cast<uint32_t>(minor)` results in 0x0000MMNN.
                           // `detail::host_to_be` correctly converts this to big-endian.
        }

        BoltError Version::from_handshake_bytes(const std::array<uint8_t, 4>& bytes, Version& out_version) {
            // Server responds with a single 32-bit big-endian version.
            uint32_t version_val_be;
            std::memcpy(&version_val_be, bytes.data(), sizeof(uint32_t));
            uint32_t version_val_host = detail::be_to_host(version_val_be);

            // Expecting 0x0000MMNN format where MM is major, NN is minor.
            if ((version_val_host >> 16) != 0) {  // Top two bytes should be zero for modern single versions
                // This could be a range proposal format if we were parsing client proposals,
                // but server response is a single version.
                // If not 0.0.X.Y, it's an unsupported format for a single version response.
                out_version = Version(0, 0);  // Reset
                return BoltError::UNSUPPORTED_PROTOCOL_VERSION;
            }

            out_version.major = static_cast<uint8_t>((version_val_host >> 8) & 0xFF);
            out_version.minor = static_cast<uint8_t>(version_val_host & 0xFF);

            // Check for 0.0 specifically, which means "no common version" if all bytes were zero.
            if (out_version.major == 0 && out_version.minor == 0) {
                bool all_zero = true;
                for (uint8_t b : bytes)
                    if (b != 0) all_zero = false;
                if (all_zero) {
                    // The handshake.cpp `parse_handshake_response` already checks this and returns HANDSHAKE_NO_COMMON_VERSION.
                    // So, here, parsing 0.0.0.0 as Version(0,0) is fine.
                }
            }
            return BoltError::SUCCESS;
        }

        // --- Definitions for extern version constants ---
        const Version V5_4(5, 4);
        const Version V5_3(5, 3);
        const Version V5_2(5, 2);
        const Version V5_1(5, 1);
        const Version V5_0(5, 0);
        const Version V4_4(4, 4);
        const Version V4_3(4, 3);
        // Add other versions if they were declared, e.g.:
        // const Version V4_2(4,2);
        // const Version V4_1(4,1);
        // const Version V4_0(4,0);
        // const Version V3_0(3,0);

        // --- Definition for get_default_proposed_versions ---
        const std::vector<Version>& get_default_proposed_versions() {
            static const std::vector<Version> DEFAULT_PROPOSED_VERSIONS_LIST = {
                V5_4, V5_3, V5_2, V5_1, V5_0, V4_4, V4_3  // Add others if defined and desired
            };
            return DEFAULT_PROPOSED_VERSIONS_LIST;
        }

    }  // namespace versions
}  // namespace boltprotocol