#include "neo4j_bolt_driver/i_socket_adaptor.h"

#include <chrono>
#include <vector>

namespace neo4j_bolt_driver {

    // Default implementation for ISocketAdaptor utility methods.
    // These methods will call the pure virtual send() and receive() methods
    // and handle the std::expected return types.

    StatusExpected ISocketAdaptor::send_all(const std::vector<uint8_t>& data, std::chrono::milliseconds timeout) {
        if (data.empty()) {
            return Success{};  // Nothing to send, operation is successful.
        }
        if (!is_connected()) {
            return std::unexpected(Error(ErrorCode::ConnectionClosedByPeer, "Socket not connected for send_all."));
        }

        const uint8_t* ptr = data.data();
        size_t remaining = data.size();
        std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::steady_clock::now();

        while (remaining > 0) {
            std::chrono::milliseconds current_chunk_timeout = timeout;
            if (timeout.count() > 0) {  // Only adjust timeout if it's not infinite
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
                if (elapsed >= timeout) {
                    return std::unexpected(Error(ErrorCode::ConnectionWriteTimeout, "Timeout during send_all operation. Sent " + std::to_string(data.size() - remaining) + " of " + std::to_string(data.size()) + " bytes."));
                }
                current_chunk_timeout = timeout - elapsed;
                // Ensure timeout is positive if it was positive initially, or zero if no time left.
                if (current_chunk_timeout.count() < 0) current_chunk_timeout = std::chrono::milliseconds(0);
            }

            auto send_result = send(ptr, remaining, current_chunk_timeout);
            if (!send_result) {  // Error occurred during send
                // The underlying send() error should be descriptive.
                // We can augment it with context if needed.
                // Example: Error augmented_error = send_result.error();
                // augmented_error.message += " (during send_all, " + std::to_string(data.size() - remaining) + " bytes sent)";
                // return std::unexpected(augmented_error);
                return std::unexpected(send_result.error());
            }

            size_t sent_this_call = send_result.value();
            if (sent_this_call == 0 && remaining > 0) {
                // If send returns 0 bytes successfully but data remains,
                // it might indicate an issue (e.g., non-blocking socket buffer full and immediate timeout).
                // The specific `send` implementation should clarify if 0 bytes sent is an error or a valid state.
                // For robust `send_all`, we might treat this as an unexpected stall if not all data sent.
                return std::unexpected(Error(ErrorCode::ConnectionWriteTimeout,  // Or a more generic stall error
                                             "Send operation stalled (0 bytes sent) during send_all. Sent " + std::to_string(data.size() - remaining) + " of " + std::to_string(data.size()) + " bytes."));
            }

            ptr += sent_this_call;
            remaining -= sent_this_call;
        }
        return Success{};
    }

    StatusExpected ISocketAdaptor::receive_all(std::vector<uint8_t>& buffer, size_t length_to_receive, std::chrono::milliseconds timeout) {
        if (length_to_receive == 0) {
            buffer.clear();
            return Success{};  // Nothing to receive.
        }
        if (!is_connected()) {
            buffer.clear();
            return std::unexpected(Error(ErrorCode::ConnectionClosedByPeer, "Socket not connected for receive_all."));
        }

        buffer.assign(length_to_receive, 0);  // Pre-allocate and zero-fill.
                                              // `assign` is better than `resize` then fill if old contents don't matter.
        uint8_t* ptr = buffer.data();
        size_t remaining = length_to_receive;
        std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::steady_clock::now();

        while (remaining > 0) {
            std::chrono::milliseconds current_chunk_timeout = timeout;
            if (timeout.count() > 0) {  // Only adjust timeout if it's not infinite
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
                if (elapsed >= timeout) {
                    buffer.resize(length_to_receive - remaining);  // Resize to actually received data
                    return std::unexpected(Error(ErrorCode::ConnectionReadTimeout, "Timeout during receive_all operation. Received " + std::to_string(length_to_receive - remaining) + " of " + std::to_string(length_to_receive) + " bytes."));
                }
                current_chunk_timeout = timeout - elapsed;
                if (current_chunk_timeout.count() < 0) current_chunk_timeout = std::chrono::milliseconds(0);
            }

            auto receive_result = receive(ptr, remaining, current_chunk_timeout);
            if (!receive_result) {                             // Error occurred during receive
                buffer.resize(length_to_receive - remaining);  // Data received so far
                return std::unexpected(receive_result.error());
            }

            size_t received_this_call = receive_result.value();
            if (received_this_call == 0) {
                // A successful receive of 0 bytes typically means the peer has performed
                // an orderly shutdown (EOF). If we haven't received all requested data,
                // this is an error for receive_all.
                buffer.resize(length_to_receive - remaining);  // Data received so far
                return std::unexpected(Error(ErrorCode::ConnectionClosedByPeer, "Connection closed by peer during receive_all. Received " + std::to_string(length_to_receive - remaining) + " of " + std::to_string(length_to_receive) + " bytes."));
            }

            ptr += received_this_call;
            remaining -= received_this_call;
        }
        return Success{};
    }

}  // namespace neo4j_bolt_driver