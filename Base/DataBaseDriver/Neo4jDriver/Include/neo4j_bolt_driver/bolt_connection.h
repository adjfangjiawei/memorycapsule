#ifndef NEO4J_BOLT_DRIVER_BOLT_CONNECTION_H
#define NEO4J_BOLT_DRIVER_BOLT_CONNECTION_H

#include <array>  // For m_negotiated_version
#include <cstdint>
#include <expected>  // For std::expected
#include <memory>    // For std::unique_ptr
#include <string>
#include <vector>

#include "neo4j_bolt_driver/bolt_protocol.h"  // For version and message tag definitions
#include "neo4j_bolt_driver/config.h"
#include "neo4j_bolt_driver/error.h"
#include "neo4j_bolt_driver/i_socket_adaptor.h"  // For ISocketAdaptor interface

// Forward declaration for PackStream types or a PackStream serializer/deserializer class
namespace neo4j_bolt_driver {
    // class PackStreamSerializer;
    // class PackStreamDeserializer;
    // struct PackStreamValue; // Or your chosen representation for PackStream data
}

namespace neo4j_bolt_driver {

    enum class BoltConnectionState {
        DISCONNECTED,
        CONNECTING_SOCKET,
        HANDSHAKING_BOLT_VERSION,
        AUTHENTICATING_HELLO,
        READY,   // Connected, authenticated, and ready for queries/transactions
        FAILED,  // Unrecoverable error state, connection unusable
        CLOSING,
        CLOSED  // Explicitly closed by client or server GOODBYE
    };

    class BoltConnection {
      public:
        // Constructor takes configuration and a factory or instance of a socket adaptor.
        // Using a unique_ptr for ISocketAdaptor allows for different socket implementations.
        BoltConnection(const BoltDriverConfig& config, std::unique_ptr<ISocketAdaptor> socket_adaptor);
        ~BoltConnection();

        // Disable copy and move semantics for now, can be added if carefully designed.
        BoltConnection(const BoltConnection&) = delete;
        BoltConnection& operator=(const BoltConnection&) = delete;
        BoltConnection(BoltConnection&&) = delete;
        BoltConnection& operator=(BoltConnection&&) = delete;

        // Attempts to establish a full Bolt connection to the Neo4j server.
        // This includes:
        // 1. TCP socket connection.
        // 2. Bolt protocol version handshake.
        // 3. HELLO message exchange for authentication and server info.
        // Returns Success or an Error.
        StatusExpected connect();

        // Closes the connection gracefully if possible (sends GOODBYE).
        // Returns Success or an Error if closing fails critically.
        StatusExpected disconnect();

        // Checks if the connection is currently in a 'READY' state.
        bool is_ready() const;

        BoltConnectionState get_state() const;
        const BoltDriverConfig& get_config() const;
        const Error* get_last_error() const;  // Returns pointer to last error, or nullptr

        // Returns the negotiated Bolt protocol version (e.g., {0x05, 0x00} for v5.0)
        // Returns an empty array if not connected or negotiation failed.
        const std::array<uint8_t, 2>& get_negotiated_bolt_version() const;

        // --- Placeholder for core message sending/receiving ---
        // These will be used by higher-level Session/Transaction objects, or directly for raw operations.
        // They will need to handle Bolt message chunking and PackStream serialization/deserialization.

        // Example: Send a pre-serialized Bolt message (byte vector)
        // StatusExpected send_bolt_message_raw(const std::vector<uint8_t>& message_bytes);

        // Example: Receive a Bolt message and get its raw bytes
        // std::expected<std::vector<uint8_t>, Error> receive_bolt_message_raw();

        // Higher level:
        // StatusExpected send_hello_message();
        // std::expected<ServerInfo, Error> process_success_after_hello(); // ServerInfo struct TBD

      private:
        // Internal helper methods for different stages of connection establishment
        StatusExpected perform_socket_connect();
        StatusExpected perform_bolt_handshake();
        StatusExpected perform_hello_authentication();
        StatusExpected send_goodbye_message();

        // Low-level message chunking logic
        // StatusExpected send_chunked_bolt_payload(protocol::MessageTag tag, const std::vector<uint8_t>& packstream_payload);
        // std::expected<std::vector<uint8_t>, Error> receive_chunked_bolt_payload(); // Returns combined payload

        void set_state(BoltConnectionState new_state);
        void set_last_error(Error err);  // Stores the error and sets state to FAILED

        BoltDriverConfig m_config;
        std::unique_ptr<ISocketAdaptor> m_socket_adaptor;
        BoltConnectionState m_current_state;
        std::array<uint8_t, 2> m_negotiated_bolt_version;  // {Major, Minor} like {0x05, 0x00}
        std::optional<Error> m_last_error;                 // Store the last critical error

        // Buffers for message construction/parsing could go here or in a dedicated message handler class
        // std::vector<uint8_t> m_send_buffer;
        // std::vector<uint8_t> m_receive_buffer;
        // std::unique_ptr<PackStreamSerializer> m_serializer;
        // std::unique_ptr<PackStreamDeserializer> m_deserializer;
    };

}  // namespace neo4j_bolt_driver

#endif  // NEO4J_BOLT_DRIVER_BOLT_CONNECTION_H