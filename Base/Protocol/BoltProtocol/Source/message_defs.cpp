#include <cstring>    // For std::memcpy if used here
#include <stdexcept>  // For Version::to_string (though not strictly needed for string conversion)

#include "boltprotocol/bolt_errors_versions.h"
#include "boltprotocol/detail/byte_order_utils.h"  // 如果 to_handshake_bytes 等在这里实现

namespace boltprotocol {
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

        bool Version::operator>(const Version& other) const {
            // a > b is equivalent to b < a
            return other < *this;
        }

        bool Version::operator<=(const Version& other) const {
            // a <= b is equivalent to !(a > b)
            return !(*this > other);
        }

        bool Version::operator>=(const Version& other) const {
            // a >= b is equivalent to !(a < b)
            return !(*this < other);
        }

        std::string Version::to_string() const {
            return std::to_string(static_cast<int>(major)) + "." + std::to_string(static_cast<int>(minor));
        }

        std::array<uint8_t, 4> Version::to_handshake_bytes() const {
            // Bolt版本字节顺序: [0, 0, minor, major] (小端在前，但通常视为一个整体)
            // 服务器期望的是大端整数，但每个字节是独立的。
            // 例如 版本 3.0 (0x03) 在网络上可能是 0x00000003
            // Neo4j驱动通常发送 [0,0,minor,major]
            // **更正**: Bolt协议规范中版本握手字节的顺序是 [major, minor, 0, 0] for highest,
            // [major, minor, 0, 0] for second highest, etc. for Bolt v1.
            // For Bolt v3+ (which uses 4-byte version numbers per proposal slot):
            // The versions are sent as 32-bit unsigned integers in big-endian byte order.
            // Example: version 4.1 (0x0104 internally for some drivers, or 0x0401 for others)
            // is encoded as [0, 0, 4, 1] or [0, 0, 1, 4] depending on interpretation.
            // The Java driver sends it as four bytes: [0, 0, minor, major]. Let's stick to that.
            return {0, 0, minor, major};
        }

        BoltError Version::from_handshake_bytes(const std::array<uint8_t, 4>& bytes, Version& out_version) {
            // 服务器返回一个它选择的版本，格式与客户端发送的相同
            // 即 bytes[3] 是主版本，bytes[2] 是次版本
            // 检查前两个字节是否为0，这是现代Bolt版本号的常见模式
            if (bytes[0] == 0 && bytes[1] == 0) {
                out_version.minor = bytes[2];
                out_version.major = bytes[3];
                // 对于全零的情况 (0.0)，可以表示“无版本”或握手失败但协议上需要返回一些东西
                // 如果major和minor都是0，这在实际Bolt版本中通常无效，除非它代表一种特殊情况。
                // Neo4j服务器在不接受任何提议版本时会关闭连接，而不是返回0.0。
                // 这里假设如果成功解析，即使是0.0，也返回SUCCESS，由调用者决定其含义。
                return BoltError::SUCCESS;
            }
            // 对于旧版Bolt或范围版本，字节格式可能不同。
            // 例如，Bolt v1 返回单个字节。Bolt v4.3+ 的范围表示法使用第一个字节。
            // 此处简化，只处理现代驱动常见的 [0,0,minor,major] 格式。
            // 如果需要支持其他格式，需要更复杂的解析逻辑。
            // 例如，如果 bytes[0] != 0，则可能是范围提议。
            // uint32_t version_val_be;
            // std::memcpy(&version_val_be, bytes.data(), 4);
            // uint32_t version_val_host = detail::be_to_host(version_val_be);
            // if ( (version_val_host >> 16) == 0 ) { // 假设是 0x0000MMNN (小端) 或 0x0000NNMM (大端)
            //     out_version.major = static_cast<uint8_t>(version_val_host & 0xFF);
            //     out_version.minor = static_cast<uint8_t>((version_val_host >> 8) & 0xFF);
            //     return BoltError::SUCCESS;
            // }
            return BoltError::UNSUPPORTED_PROTOCOL_VERSION;  // 或 INVALID_MESSAGE_FORMAT
        }

        // --- Definitions for extern version constants (declared in bolt_errors_versions.h) ---
        const Version V5_4(5, 4);
        const Version V5_3(5, 3);
        const Version V5_2(5, 2);
        const Version V5_1(5, 1);
        const Version V5_0(5, 0);
        const Version V4_4(4, 4);
        const Version V4_3(4, 3);  // 确保 V4_3 被定义
        // const Version V4_2(4,2);
        // const Version V4_1(4,1);
        // const Version V4_0(4,0);
        // const Version V3_0(3,0);

        // --- Definition for get_default_proposed_versions (declared in bolt_errors_versions.h) ---
        const std::vector<Version>& get_default_proposed_versions() {
            // 驱动应按优先顺序列出它支持的版本，从高到低
            static const std::vector<Version> DEFAULT_PROPOSED_VERSIONS_LIST = {
                V5_4, V5_3, V5_2, V5_1, V5_0, V4_4, V4_3  // 确保 V4_3 在这里
                // Add V4_2, V4_1 etc. if they are defined constants and desired in default list
            };
            return DEFAULT_PROPOSED_VERSIONS_LIST;
        }

    }  // namespace versions

    // --- 修正：将 DEFAULT_USER_AGENT_FORMAT_STRING 和 Value::operator== 的定义移到 bolt_core_types.cpp 或一个新的 bolt_message_defs.cpp (如果它们在 message_defs.h 中声明为 extern) ---
    // 为了保持 bolt_errors_versions.cpp 的纯粹性，这些定义不应在此处。
    // 假设它们在别处定义。

}  // namespace boltprotocol