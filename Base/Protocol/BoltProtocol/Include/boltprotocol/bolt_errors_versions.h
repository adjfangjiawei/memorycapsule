#ifndef BOLTPROTOCOL_ERRORS_VERSIONS_H
#define BOLTPROTOCOL_ERRORS_VERSIONS_H

#include <array>
#include <cstdint>
#include <string>  // For Version::to_string (建议添加)
#include <vector>

namespace boltprotocol {

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

            // 核心比较操作符
            bool operator<(const Version& other) const;
            bool operator==(const Version& other) const;

            // 从核心操作符派生的其他比较操作符
            bool operator!=(const Version& other) const;
            bool operator>(const Version& other) const;
            bool operator<=(const Version& other) const;
            bool operator>=(const Version& other) const;

            std::string to_string() const;  // 便于调试

            std::array<uint8_t, 4> to_handshake_bytes() const;
            static BoltError from_handshake_bytes(const std::array<uint8_t, 4>& bytes, Version& out_version);
        };

        // 声明版本常量
        extern const Version V5_4, V5_3, V5_2, V5_1, V5_0, V4_4, V4_3;  // 确保 V4_3 也被声明
        // extern const Version V4_2, V4_1, V4_0, V3_0;

        // 声明函数以获取默认建议版本
        extern const std::vector<Version>& get_default_proposed_versions();
    }  // namespace versions

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_ERRORS_VERSIONS_H