#include "boltprotocol/handshake.h"

#include <algorithm>  // For std::min
#include <cstring>    // For std::memcpy, std::memset
#include <iostream>   // For stream operations (included via iostream in handshake.h but good practice)

#include "boltprotocol/detail/byte_order_utils.h"  // For host_to_be, be_to_host

namespace boltprotocol {

    BoltError build_handshake_request(const std::vector<versions::Version>& proposed_versions, std::array<uint8_t, HANDSHAKE_REQUEST_SIZE_BYTES>& out_handshake_bytes) {
        if (proposed_versions.empty()) {
            return BoltError::INVALID_ARGUMENT;
        }

        // 1. Magic Preamble
        uint32_t preamble_be = detail::host_to_be(BOLT_MAGIC_PREAMBLE);
        std::memcpy(out_handshake_bytes.data(), &preamble_be, sizeof(preamble_be));
        size_t offset = sizeof(preamble_be);

        // 2. Proposed Versions
        // Fill with up to HANDSHAKE_NUM_PROPOSED_VERSIONS
        size_t num_to_write = std::min(proposed_versions.size(), HANDSHAKE_NUM_PROPOSED_VERSIONS);
        for (size_t i = 0; i < num_to_write; ++i) {
            uint32_t version_int_host = (static_cast<uint32_t>(proposed_versions[i].minor) << 24) |  // Corrected bit shift for Bolt's BE representation
                                        (static_cast<uint32_t>(proposed_versions[i].major) << 16);   // Major and minor are in the higher bytes
                                                                                                     // The other two bytes are for patch and tweak (usually 0 for handshake)
                                                                                                     // Bolt v1-v3: 0000<Major><Minor> (No, Bolt spec is <Minor><Patch> <Tweak><Major> or similar, let's recheck)
            // Bolt handshake format for versions is specific:
            // The 4 bytes are, in network byte order (big-endian):
            // No Version (00000000)
            // Version 1 (00000001) (implicitly Bolt 1.0)
            // For Bolt >= 3.0:  . . <Major> <Minor>
            // E.g., Bolt 4.1 is 00 00 04 01
            // E.g., Bolt 5.0 is 00 00 05 00

            // The versions::Version::to_handshake_int() creates <minor><major> in lower two bytes.
            // We need to ensure it's correctly placed for big-endian.
            // Let's use the provided to_handshake_bytes that already handles htonl.
            // No, versions::Version::to_handshake_int() and to_handshake_bytes() were designed
            // for the old interpretation. Let's fix this here directly according to Bolt spec.

            uint32_t version_for_handshake = 0;
            // For Bolt protocol versions, the most significant byte is usually 0,
            // then the next byte is 0, then Major, then Minor.
            // So for version X.Y, it would be 0x0000XY (in hex representation of bytes).
            version_for_handshake = (static_cast<uint32_t>(proposed_versions[i].major) << 8) | (static_cast<uint32_t>(proposed_versions[i].minor));
            // This results in 0x0000<Major><Minor> in host order.
            // Then host_to_be will convert it.

            uint32_t version_be = detail::host_to_be(version_for_handshake);
            std::memcpy(out_handshake_bytes.data() + offset, &version_be, HANDSHAKE_VERSION_SIZE_BYTES);
            offset += HANDSHAKE_VERSION_SIZE_BYTES;
        }

        // Fill remaining slots with 0 (No Version) if fewer than 4 versions provided
        if (num_to_write < HANDSHAKE_NUM_PROPOSED_VERSIONS) {
            uint32_t no_version_be = detail::host_to_be(static_cast<uint32_t>(0));
            for (size_t i = num_to_write; i < HANDSHAKE_NUM_PROPOSED_VERSIONS; ++i) {
                std::memcpy(out_handshake_bytes.data() + offset, &no_version_be, HANDSHAKE_VERSION_SIZE_BYTES);
                offset += HANDSHAKE_VERSION_SIZE_BYTES;
            }
        }
        return BoltError::SUCCESS;
    }

    BoltError parse_handshake_response(const std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES>& server_response, versions::Version& out_negotiated_version) {
        uint32_t version_int_be;
        std::memcpy(&version_int_be, server_response.data(), HANDSHAKE_RESPONSE_SIZE_BYTES);
        uint32_t version_int_host = detail::be_to_host(version_int_be);

        if (version_int_host == 0) {
            return BoltError::HANDSHAKE_NO_COMMON_VERSION;
        }

        // Extract Major and Minor. For 0x0000<Major><Minor>
        out_negotiated_version.major = static_cast<uint8_t>((version_int_host >> 8) & 0xFF);
        out_negotiated_version.minor = static_cast<uint8_t>(version_int_host & 0xFF);

        // A minimal check, real server might send specific older single-byte versions.
        // For simplicity, we assume modern Major.Minor format here.
        // E.g. Bolt 1.0 might be sent as 0x00000001 by some older servers, handle if needed.
        // If version_int_host is, for example, 1, it means Bolt 1.0.
        if (out_negotiated_version.major == 0 && out_negotiated_version.minor == 0 && version_int_host != 0) {
            // This could be an old single number version, e.g. 1 for Bolt 1.0
            // For simplicity, let's assume major.minor format or error for now.
            // A more robust parser would check known single-value versions (1, 2).
            if (version_int_host == 1) {  // Example: Treat '1' as 1.0
                out_negotiated_version.major = 1;
                out_negotiated_version.minor = 0;
            } else {
                // Unrecognized single number version or invalid format if major and minor are 0 but host_int isn't.
                return BoltError::DESERIALIZATION_ERROR;
            }
        }

        return BoltError::SUCCESS;
    }

    BoltError perform_handshake(std::ostream& ostream, std::istream& istream, const std::vector<versions::Version>& proposed_versions, versions::Version& out_negotiated_version) {
        std::array<uint8_t, HANDSHAKE_REQUEST_SIZE_BYTES> request_bytes;
        BoltError err = build_handshake_request(proposed_versions, request_bytes);
        if (err != BoltError::SUCCESS) {
            return err;
        }

        ostream.write(reinterpret_cast<const char*>(request_bytes.data()), request_bytes.size());
        if (ostream.fail()) {
            return BoltError::NETWORK_ERROR;
        }
        ostream.flush();  // Ensure it's sent
        if (ostream.fail()) {
            return BoltError::NETWORK_ERROR;
        }

        std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES> response_bytes;
        istream.read(reinterpret_cast<char*>(response_bytes.data()), response_bytes.size());

        if (istream.fail() || istream.eof()) {  // Read failed or not enough bytes
            return BoltError::NETWORK_ERROR;
        }
        if (static_cast<size_t>(istream.gcount()) != HANDSHAKE_RESPONSE_SIZE_BYTES) {
            return BoltError::NETWORK_ERROR;  // Didn't read expected number of bytes
        }

        return parse_handshake_response(response_bytes, out_negotiated_version);
    }

}  // namespace boltprotocol