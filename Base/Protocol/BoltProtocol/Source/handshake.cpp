#include "boltprotocol/handshake.h"  // 包含声明

#include <algorithm>  // For std::min
#include <cstring>    // For std::memcpy
// <iostream> is not directly needed here anymore for perform_handshake

#include "boltprotocol/detail/byte_order_utils.h"  // For detail::host_to_be

namespace boltprotocol {

    // build_handshake_request: Constructs the 20-byte handshake request.
    BoltError build_handshake_request(const std::vector<versions::Version>& proposed_versions, std::array<uint8_t, HANDSHAKE_REQUEST_SIZE_BYTES>& out_handshake_bytes) {
        if (proposed_versions.empty()) {
            return BoltError::INVALID_ARGUMENT;
        }

        // 1. Magic Preamble (4 bytes, Big Endian)
        uint32_t preamble_be = detail::host_to_be(BOLT_MAGIC_PREAMBLE);  // BOLT_MAGIC_PREAMBLE from message_defs.h
        std::memcpy(out_handshake_bytes.data(), &preamble_be, sizeof(preamble_be));
        size_t current_offset = sizeof(preamble_be);

        // 2. Proposed Versions (4 versions, each 4 bytes, Big Endian)
        size_t num_versions_to_write = std::min(proposed_versions.size(), HANDSHAKE_NUM_PROPOSED_VERSIONS);

        for (size_t i = 0; i < num_versions_to_write; ++i) {
            // Version::to_handshake_bytes() already returns the 4 bytes in the correct format (00 00 Maj Min)
            // and handles endianness *if* it were encoding a multi-byte representation of major/minor itself.
            // However, the Bolt spec for handshake versions is simpler: the 4-byte int is 0.0.Major.Minor.
            // So we construct this 32-bit int and then convert to big endian.
            uint32_t version_int32_for_handshake = 0;  // Example: For 5.4, this would be 0x00000504
            version_int32_for_handshake = (static_cast<uint32_t>(proposed_versions[i].major) << 8) | (static_cast<uint32_t>(proposed_versions[i].minor));

            uint32_t version_be = detail::host_to_be(version_int32_for_handshake);
            std::memcpy(out_handshake_bytes.data() + current_offset, &version_be, HANDSHAKE_VERSION_SIZE_BYTES);
            current_offset += HANDSHAKE_VERSION_SIZE_BYTES;
        }

        // Fill remaining version slots with "No Version" (all zeros)
        if (num_versions_to_write < HANDSHAKE_NUM_PROPOSED_VERSIONS) {
            uint32_t no_version_int_be = detail::host_to_be(static_cast<uint32_t>(0));  // 0x00000000
            for (size_t i = num_versions_to_write; i < HANDSHAKE_NUM_PROPOSED_VERSIONS; ++i) {
                std::memcpy(out_handshake_bytes.data() + current_offset, &no_version_int_be, HANDSHAKE_VERSION_SIZE_BYTES);
                current_offset += HANDSHAKE_VERSION_SIZE_BYTES;
            }
        }
        return BoltError::SUCCESS;
    }

    // parse_handshake_response: Parses the 4-byte server response.
    BoltError parse_handshake_response(const std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES>& server_response_bytes, versions::Version& out_negotiated_version) {
        // Version::from_handshake_bytes directly uses the byte array.
        BoltError err = versions::Version::from_handshake_bytes(server_response_bytes, out_negotiated_version);

        if (err != BoltError::SUCCESS) {
            return err;  // Could be UNSUPPORTED_PROTOCOL_VERSION if format is unexpected by from_handshake_bytes
        }

        // Specific check for "No common version" which is represented by all zeros.
        // Version::from_handshake_bytes might return 0.0 as a valid version for all zeros.
        if (out_negotiated_version.major == 0 && out_negotiated_version.minor == 0) {
            // Check if all bytes in server_response_bytes are actually zero
            bool all_zero = true;
            for (uint8_t byte : server_response_bytes) {
                if (byte != 0) {
                    all_zero = false;
                    break;
                }
            }
            if (all_zero) {
                return BoltError::HANDSHAKE_NO_COMMON_VERSION;
            }
            // If it parsed as 0.0 but bytes weren't all zero, it's an odd case,
            // but Version::from_handshake_bytes should ideally handle invalid formats.
            // For now, if it parses to 0.0 and wasn't all zeros, treat as success with 0.0 (unlikely scenario).
        }
        return BoltError::SUCCESS;
    }

    // The definition of perform_handshake (template function) is now in handshake_impl.hpp

}  // namespace boltprotocol