#include "neo4j_bolt_driver/bolt_connection.h"

#include <array>  // For handshake version bytes
#include <vector>

#include "neo4j_bolt_driver/bolt_protocol.h"  // For protocol constants

// For htons/ntohs if directly manipulating network byte order for chunk sizes, though
// a utility class for byte order conversion is cleaner.
// #if defined(_WIN32) || defined(_WIN64)
// #include <winsock2.h> // For htons, ntohs on Windows
// #else
// #include <arpa/inet.h> // For htons, ntohs on POSIX
// #endif

namespace neo4j_bolt_driver {

    BoltConnection::BoltConnection(const BoltDriverConfig& config, std::unique_ptr<ISocketAdaptor> socket_adaptor) : m_config(config), m_socket_adaptor(std::move(socket_adaptor)), m_current_state(BoltConnectionState::DISCONNECTED), m_negotiated_bolt_version({0, 0}) {
        if (!m_socket_adaptor) {
            // This is a programming error, socket adaptor must be provided.
            // In a non-exception world, this constructor might need to signal failure.
            // For now, assume it's always provided. Or make constructor return std::expected.
            // Or set a permanent error state.
            set_last_error(Error(ErrorCode::ConfigurationError, "Socket adaptor not provided to BoltConnection."));
            set_state(BoltConnectionState::FAILED);
        }
    }

    BoltConnection::~BoltConnection() {
        if (m_current_state != BoltConnectionState::DISCONNECTED && m_current_state != BoltConnectionState::CLOSED && m_current_state != BoltConnectionState::FAILED) {  // Avoid disconnect if already failed badly
            // Best effort disconnect, ignore result for destructor
            disconnect();
        }
    }

    StatusExpected BoltConnection::connect() {
        if (m_current_state != BoltConnectionState::DISCONNECTED && m_current_state != BoltConnectionState::CLOSED) {
            if (is_ready()) return Success{};  // Already connected and ready
            return std::unexpected(Error(ErrorCode::InvalidState, "Connection attempt in an invalid state: " + std::to_string(static_cast<int>(m_current_state))));
        }
        m_last_error.reset();  // Clear previous errors

        // Stage 1: Socket Connect
        set_state(BoltConnectionState::CONNECTING_SOCKET);
        auto socket_connect_status = perform_socket_connect();
        if (!socket_connect_status) {
            set_last_error(socket_connect_status.error());
            set_state(BoltConnectionState::FAILED);  // Or FAILED_SOCKET_CONNECT
            return socket_connect_status;
        }

        // Stage 2: Bolt Handshake
        set_state(BoltConnectionState::HANDSHAKING_BOLT_VERSION);
        auto handshake_status = perform_bolt_handshake();
        if (!handshake_status) {
            set_last_error(handshake_status.error());
            set_state(BoltConnectionState::FAILED);  // Or FAILED_HANDSHAKE
            m_socket_adaptor->disconnect();          // Clean up socket
            return handshake_status;
        }

        // Stage 3: HELLO Authentication
        set_state(BoltConnectionState::AUTHENTICATING_HELLO);
        auto hello_status = perform_hello_authentication();
        if (!hello_status) {
            set_last_error(hello_status.error());
            set_state(BoltConnectionState::FAILED);  // Or FAILED_AUTHENTICATION
            m_socket_adaptor->disconnect();          // Clean up socket
            return hello_status;
        }

        set_state(BoltConnectionState::READY);
        return Success{};
    }

    StatusExpected BoltConnection::disconnect() {
        if (m_current_state == BoltConnectionState::DISCONNECTED || m_current_state == BoltConnectionState::CLOSED) {
            return Success{};  // Already disconnected or closed
        }
        set_state(BoltConnectionState::CLOSING);

        StatusExpected goodbye_status = Success{};
        if (m_socket_adaptor && m_socket_adaptor->is_connected()) {  // Only send GOODBYE if socket seems alive
            // TODO: Implement send_goodbye_message (which would involve PackStream and chunking)
            // goodbye_status = send_goodbye_message();
            // If goodbye fails, we still proceed to close the socket, but we might log the error.
            // For now, assume it's part of the socket disconnect or not critical for this stage.
        }

        if (m_socket_adaptor) {
            m_socket_adaptor->disconnect();
        }

        set_state(BoltConnectionState::CLOSED);
        m_negotiated_bolt_version = {0, 0};  // Reset negotiated version
        return goodbye_status;               // Or just Success{} if goodbye errors are not propagated critically
    }

    bool BoltConnection::is_ready() const {
        return m_current_state == BoltConnectionState::READY;
    }

    BoltConnectionState BoltConnection::get_state() const {
        return m_current_state;
    }

    const BoltDriverConfig& BoltConnection::get_config() const {
        return m_config;
    }

    const Error* BoltConnection::get_last_error() const {
        if (m_last_error) {
            return &(*m_last_error);
        }
        return nullptr;
    }

    const std::array<uint8_t, 2>& BoltConnection::get_negotiated_bolt_version() const {
        return m_negotiated_bolt_version;
    }

    // --- Private Helper Implementations ---

    void BoltConnection::set_state(BoltConnectionState new_state) {
        // TODO: Add logging here if desired: "BoltConnection state changing from X to Y"
        m_current_state = new_state;
    }
    void BoltConnection::set_last_error(Error err) {
        m_last_error = std::move(err);
    }

    StatusExpected BoltConnection::perform_socket_connect() {
        if (!m_socket_adaptor) {  // Should have been caught in constructor
            return std::unexpected(Error(ErrorCode::DriverInternalError, "Socket adaptor is null in perform_socket_connect."));
        }
        auto connect_status = m_socket_adaptor->connect(m_config.host, m_config.port, m_config.connection_timeout);
        if (!connect_status) {
            // The error from socket_adaptor->connect() should be specific enough.
            return std::unexpected(connect_status.error());
        }
        return Success{};
    }

    StatusExpected BoltConnection::perform_bolt_handshake() {
        // 1. Send Bolt Magic Preamble (0x6060B017)
        // 2. Send 4 proposed Bolt versions (each 4 bytes, big-endian in the list on the wire)
        // 3. Receive 1 agreed Bolt version (2 bytes, major.minor, network byte order/big-endian)

        // --- 1. Send Magic Preamble ---
        std::array<uint8_t, 4> magic_preamble_bytes;
        uint32_t magic = protocol::BOLT_MAGIC_PREAMBLE;  // Already in network byte order if defined as 0x6060B017
        magic_preamble_bytes[0] = (magic >> 24) & 0xFF;
        magic_preamble_bytes[1] = (magic >> 16) & 0xFF;
        magic_preamble_bytes[2] = (magic >> 8) & 0xFF;
        magic_preamble_bytes[3] = (magic >> 0) & 0xFF;

        std::vector<uint8_t> handshake_request;
        handshake_request.insert(handshake_request.end(), magic_preamble_bytes.begin(), magic_preamble_bytes.end());

        // --- 2. Send Proposed Versions ---
        // Bolt spec: "The versions are ordered by preference, most preferred first.
        // Each version is specified as a 4-tuple: (Major, Minor, Patch, Revision)."
        // However, on the wire, it's just a sequence of 4-byte version proposals.
        // Example: For Bolt 5.0 (0x05 0x00 0x00 0x00), then 4.4 (0x04 0x04 0x00 0x00)
        // The spec can be a bit confusing here. The official drivers typically send 4 versions,
        // each represented by 4 bytes on the wire. The bytes for *each* version proposal on the wire are
        // {0, 0, Minor, Major} if thinking little-endian for the 32-bit uint.
        // Or, more directly: {0x00, 0x00, proposed_minor, proposed_major} for each of the 4 slots.
        // Let's re-check the spec carefully for the *exact* byte order of the *version proposal list*.

        // The Bolt August 2023 spec (v5.4) section 4.1 Handshake:
        // Client -> Server:
        //   MAGIC_PREAMBLE          (4 bytes)
        //   SUPPORTED_VERSION_1     (4 bytes)
        //   SUPPORTED_VERSION_2     (4 bytes)
        //   SUPPORTED_VERSION_3     (4 bytes)
        //   SUPPORTED_VERSION_4     (4 bytes)
        // Each SUPPORTED_VERSION_N is a 32-bit unsigned integer. The byte order is BIG ENDIAN.
        // The value itself is composed: 00 00 Minor Major. E.g., for 5.0 -> 0x00000005. (Major=5, Minor=0)
        // No, that's not right. The value is composed of Major.Minor.
        // E.g., Bolt 1.0 (deprecated) would be 0x00000100. (Major 0, Minor 1 interpreted as 1.0)
        // For modern Bolt: vX.Y is typically (Y << 8) | X. Or simply X.Y encoded.
        // The `protocol::versions::V5_0` etc. are {Major, Minor, Patch, Revision} in memory.
        // On the wire, each 4-byte proposed version is `00 00 Minor Major` (Big Endian for the 32-bit uint).
        // So for 5.0 (Major=5, Minor=0), it would be sent as bytes `00 00 00 05`.
        // For 4.4 (Major=4, Minor=4), it would be sent as bytes `00 00 04 04`.

        const auto& proposed_versions_list = protocol::versions::get_default_proposed_versions();
        int versions_to_send = 0;
        for (const auto& version_array : proposed_versions_list) {
            if (versions_to_send >= 4) break;  // Send at most 4 versions
            // version_array is {Major, Minor, Patch, Revision}
            handshake_request.push_back(0x00);              // Higher byte of higher word
            handshake_request.push_back(0x00);              // Lower byte of higher word
            handshake_request.push_back(version_array[1]);  // Minor version part
            handshake_request.push_back(version_array[0]);  // Major version part
            versions_to_send++;
        }
        // If fewer than 4 versions in our list, pad with 0s for the remaining version slots
        for (int i = versions_to_send; i < 4; ++i) {
            handshake_request.push_back(0x00);
            handshake_request.push_back(0x00);
            handshake_request.push_back(0x00);
            handshake_request.push_back(0x00);
        }

        auto send_status = m_socket_adaptor->send_all(handshake_request, m_config.socket_timeout);
        if (!send_status) {
            return std::unexpected(Error(ErrorCode::BoltHandshakeFailed, "Failed to send handshake request: " + send_status.error().message, send_status.error().code));
        }

        // --- 3. Receive Agreed Version ---
        std::vector<uint8_t> agreed_version_bytes(2);  // Server replies with 2 bytes: {Major, Minor} in network order
        auto receive_status = m_socket_adaptor->receive_all(agreed_version_bytes, 2, m_config.socket_timeout);
        if (!receive_status) {
            if (receive_status.error().code == ErrorCode::ConnectionClosedByPeer && agreed_version_bytes.empty()) {
                // This is a common case if server doesn't support any proposed versions.
                return std::unexpected(Error(ErrorCode::BoltUnsupportedVersion, "Server closed connection during handshake; likely no supported Bolt version. " + receive_status.error().message));
            }
            return std::unexpected(Error(ErrorCode::BoltHandshakeFailed, "Failed to receive agreed Bolt version: " + receive_status.error().message, receive_status.error().code));
        }

        if (agreed_version_bytes[0] == 0 && agreed_version_bytes[1] == 0) {
            // Server rejected all versions by responding with 0.0
            return std::unexpected(Error(ErrorCode::BoltUnsupportedVersion, "Server rejected all proposed Bolt versions (responded with 0.0)."));
        }

        // Store the negotiated version (Major, Minor)
        m_negotiated_bolt_version[0] = agreed_version_bytes[0];  // Major
        m_negotiated_bolt_version[1] = agreed_version_bytes[1];  // Minor

        // TODO: Validate that the agreed_version is one we actually proposed, or at least one we can handle.
        // For now, we accept what the server gives if it's not 0.0.

        return Success{};
    }

    StatusExpected BoltConnection::perform_hello_authentication() {
        // This is a major piece of work involving:
        // 1. Constructing the HELLO message using PackStream serialization.
        //    The HELLO message is a Bolt Structure:
        //    Tag: 0x01 (HELLO)
        //    Fields: 1
        //      Field 0: Map of parameters (e.g., user_agent, scheme, principal, credentials, routing context, etc.)
        //
        // 2. Sending the serialized HELLO message, correctly chunked.
        //    Each Bolt message is sent as a sequence of chunks.
        //    Each chunk:
        //      - 2-byte unsigned short: chunk_size (big-endian)
        //      - `chunk_size` bytes of payload
        //    A message ends with a 0-length chunk (0x0000).
        //
        // 3. Receiving the server's response: SUCCESS or FAILURE.
        //    SUCCESS (Tag 0x70): Contains a Map with server info (connection_id, server agent, etc.)
        //    FAILURE (Tag 0x7F): Contains a Map with error code and message.
        //    These also need to be received (de-chunked) and deserialized using PackStream.

        // Placeholder:
        // if (!m_serializer || !m_deserializer) {
        //    return std::unexpected(Error(ErrorCode::DriverInternalError, "PackStream handlers not initialized."));
        // }
        //
        // std::vector<uint8_t> hello_payload_bytes;
        // auto serialization_status = m_serializer->serialize_hello(m_config, hello_payload_bytes);
        // if (!serialization_status) return std::unexpected(serialization_status.error());
        //
        // auto send_status = send_chunked_bolt_payload(protocol::MessageTag::HELLO, hello_payload_bytes);
        // if (!send_status) return std::unexpected(send_status.error());
        //
        // auto response_payload_bytes_expected = receive_chunked_bolt_payload();
        // if (!response_payload_bytes_expected) return std::unexpected(response_payload_bytes_expected.error());
        //
        // auto deserialization_status = m_deserializer->deserialize_server_response(response_payload_bytes_expected.value(), ...);
        // ... handle SUCCESS or FAILURE ...

        // For now, returning a "Not Implemented" error.
        return std::unexpected(Error(ErrorCode::FeatureNotImplemented, "perform_hello_authentication is not yet implemented."));
    }

    StatusExpected BoltConnection::send_goodbye_message() {
        // Similar to HELLO: construct GOODBYE (Tag 0x02, 0 fields), serialize, send chunked.
        // No response expected for GOODBYE from server typically.
        return std::unexpected(Error(ErrorCode::FeatureNotImplemented, "send_goodbye_message is not yet implemented."));
    }

}  // namespace neo4j_bolt_driver