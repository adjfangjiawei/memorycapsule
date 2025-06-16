#include "boltprotocol/handshake.h"

#include <algorithm>  // For std::min
#include <cstring>    // For std::memcpy
#include <iostream>   // For stream operations (std::ostream, std::istream)

#include "boltprotocol/detail/byte_order_utils.h"  // For host_to_be for BOLT_MAGIC_PREAMBLE

namespace boltprotocol {

    // build_handshake_request: Constructs the 20-byte handshake request.
    BoltError build_handshake_request(const std::vector<versions::Version>& proposed_versions, std::array<uint8_t, HANDSHAKE_REQUEST_SIZE_BYTES>& out_handshake_bytes) {
        if (proposed_versions.empty()) {
            return BoltError::INVALID_ARGUMENT;
        }

        // 1. Magic Preamble (4 bytes, Big Endian)
        uint32_t preamble_be = detail::host_to_be(BOLT_MAGIC_PREAMBLE);
        std::memcpy(out_handshake_bytes.data(), &preamble_be, sizeof(preamble_be));
        size_t current_offset = sizeof(preamble_be);

        // 2. Proposed Versions (4 versions, each 4 bytes, Big Endian)
        size_t num_versions_to_write = std::min(proposed_versions.size(), HANDSHAKE_NUM_PROPOSED_VERSIONS);

        for (size_t i = 0; i < num_versions_to_write; ++i) {
            // Construct the 32-bit integer representing the version for handshake.
            // Bolt version X.Y is represented as 00 00 0X 0Y in Big Endian.
            uint32_t version_for_handshake = 0;  // Initialize
            version_for_handshake = (static_cast<uint32_t>(proposed_versions[i].major) << 8) | (static_cast<uint32_t>(proposed_versions[i].minor));

            uint32_t version_be = detail::host_to_be(version_for_handshake);  // Convert to Big Endian
            std::memcpy(out_handshake_bytes.data() + current_offset, &version_be, HANDSHAKE_VERSION_SIZE_BYTES);
            current_offset += HANDSHAKE_VERSION_SIZE_BYTES;
        }

        // Fill remaining version slots with "No Version" (all zeros)
        if (num_versions_to_write < HANDSHAKE_NUM_PROPOSED_VERSIONS) {
            uint32_t no_version_int_be = detail::host_to_be(static_cast<uint32_t>(0));
            for (size_t i = num_versions_to_write; i < HANDSHAKE_NUM_PROPOSED_VERSIONS; ++i) {
                std::memcpy(out_handshake_bytes.data() + current_offset, &no_version_int_be, HANDSHAKE_VERSION_SIZE_BYTES);
                current_offset += HANDSHAKE_VERSION_SIZE_BYTES;
            }
        }
        return BoltError::SUCCESS;
    }

    // parse_handshake_response: Parses the 4-byte server response.
    BoltError parse_handshake_response(const std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES>& server_response_bytes, versions::Version& out_negotiated_version) {
        BoltError err = versions::Version::from_handshake_bytes(server_response_bytes, out_negotiated_version);
        if (err != BoltError::SUCCESS) {
            return err;
        }

        if (out_negotiated_version.major == 0 && out_negotiated_version.minor == 0) {
            // This typically means the server sent 0x00000000, indicating no common version.
            // The from_handshake_bytes might return SUCCESS for 0.0, so we explicitly check here.
            return BoltError::HANDSHAKE_NO_COMMON_VERSION;
        }
        return BoltError::SUCCESS;
    }

    // perform_handshake: Executes the full handshake over provided streams.
    BoltError perform_handshake(std::ostream& ostream, std::istream& istream, const std::vector<versions::Version>& proposed_versions, versions::Version& out_negotiated_version) {
        std::array<uint8_t, HANDSHAKE_REQUEST_SIZE_BYTES> handshake_request_bytes;
        BoltError build_err = build_handshake_request(proposed_versions, handshake_request_bytes);
        if (build_err != BoltError::SUCCESS) {
            return build_err;
        }

        if (ostream.fail()) return BoltError::NETWORK_ERROR;
        ostream.write(reinterpret_cast<const char*>(handshake_request_bytes.data()), HANDSHAKE_REQUEST_SIZE_BYTES);
        if (ostream.fail()) {
            return BoltError::NETWORK_ERROR;
        }
        ostream.flush();
        if (ostream.fail()) {
            return BoltError::NETWORK_ERROR;
        }

        std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES> server_response_bytes;
        if (istream.fail()) return BoltError::NETWORK_ERROR;
        istream.read(reinterpret_cast<char*>(server_response_bytes.data()), HANDSHAKE_RESPONSE_SIZE_BYTES);

        if (istream.fail()) {
            return BoltError::NETWORK_ERROR;
        }
        if (static_cast<size_t>(istream.gcount()) != HANDSHAKE_RESPONSE_SIZE_BYTES) {
            return BoltError::NETWORK_ERROR;
        }

        return parse_handshake_response(server_response_bytes, out_negotiated_version);
    }

}  // namespace boltprotocol