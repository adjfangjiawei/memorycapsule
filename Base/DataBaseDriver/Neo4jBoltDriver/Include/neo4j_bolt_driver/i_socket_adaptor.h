#ifndef NEO4J_BOLT_DRIVER_I_SOCKET_ADAPTOR_H
#define NEO4J_BOLT_DRIVER_I_SOCKET_ADAPTOR_H

#include <chrono>
#include <cstddef>   // For size_t
#include <cstdint>   // For uint8_t, uint16_t
#include <expected>  // C++23 standard
#include <string>
#include <vector>

#include "neo4j_bolt_driver/error.h"  // For the Error type

namespace neo4j_bolt_driver {

    // Represents a successful operation with no specific return value for std::expected.
    struct Success {};

    // Common alias for status-only results (success or error)
    using StatusExpected = std::expected<Success, Error>;

    // Interface class for abstracting socket operations.
    class ISocketAdaptor {
      public:
        virtual ~ISocketAdaptor() = default;

        // Attempts to connect to the specified host and port.
        // Returns Success on successful connection, or an Error otherwise.
        virtual StatusExpected connect(const std::string& host, uint16_t port, std::chrono::milliseconds timeout) = 0;

        // Disconnects from the server.
        // This operation is typically best-effort and might not have a distinct failure mode
        // that needs to be reported, unless it fails to release resources, for instance.
        // For simplicity, it can be void or return a StatusExpected if specific failures are relevant.
        virtual void disconnect() = 0;  // Or: virtual StatusExpected disconnect() = 0;

        // Checks if the socket is currently connected.
        virtual bool is_connected() const = 0;

        // Sends data over the socket.
        // Returns the number of bytes actually sent on success, or an Error on failure.
        // A successful send of 0 bytes is possible if `size` was 0.
        virtual std::expected<size_t, Error> send(const uint8_t* data, size_t size, std::chrono::milliseconds timeout) = 0;

        // Receives data from the socket.
        // Returns the number of bytes actually received on success, or an Error on failure.
        // A successful receive of 0 bytes typically indicates an orderly shutdown by the peer.
        virtual std::expected<size_t, Error> receive(uint8_t* buffer, size_t size, std::chrono::milliseconds timeout) = 0;

        // Utility method to send all data in the provided vector.
        // Returns Success if all data was sent, or an Error otherwise.
        // The Error might indicate how many bytes were sent before failure, if relevant.
        virtual StatusExpected send_all(const std::vector<uint8_t>& data, std::chrono::milliseconds timeout);

        // Utility method to receive a specific number of bytes into the provided vector.
        // The buffer is an out-parameter, modified on success.
        // Returns Success if all 'length_to_receive' bytes were received, or an Error otherwise.
        // If an error occurs, the buffer might contain partially received data.
        virtual StatusExpected receive_all(std::vector<uint8_t>& buffer, size_t length_to_receive, std::chrono::milliseconds timeout);
    };

}  // namespace neo4j_bolt_driver

#endif  // NEO4J_BOLT_DRIVER_I_SOCKET_ADAPTOR_H