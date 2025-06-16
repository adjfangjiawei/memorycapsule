#include "boltprotocol/chunking.h"

#include <algorithm>  // For std::min
#include <cstring>    // For std::memcpy (though not strictly needed if only writing from vector)
#include <exception>  // For std::bad_alloc
#include <iostream>   // For stream operations (std::ostream, std::istream)
#include <vector>     // For std::vector manipulations

#include "boltprotocol/detail/byte_order_utils.h"  // For host_to_be, be_to_host

namespace boltprotocol {

    // --- ChunkedWriter Implementation ---

    ChunkedWriter::ChunkedWriter(std::ostream& stream) : stream_(stream), last_error_(BoltError::SUCCESS) {
        if (stream_.fail()) {                        // Initial check of stream state
            set_error(BoltError::INVALID_ARGUMENT);  // Or NETWORK_ERROR if stream is already bad
        }
    }

    void ChunkedWriter::set_error(BoltError err) {
        if (last_error_ == BoltError::SUCCESS && err != BoltError::SUCCESS) {
            last_error_ = err;
        }
    }

    // Private helper to write the 2-byte chunk header
    BoltError ChunkedWriter::write_chunk_header(uint16_t chunk_payload_size) {
        if (has_error()) return last_error_;  // Don't proceed if already in error state

        uint16_t size_be = detail::host_to_be(chunk_payload_size);
        if (stream_.fail()) {
            set_error(BoltError::NETWORK_ERROR);
            return last_error_;
        }
        stream_.write(reinterpret_cast<const char*>(&size_be), sizeof(size_be));
        if (stream_.fail()) {
            set_error(BoltError::NETWORK_ERROR);
            return BoltError::NETWORK_ERROR;
        }
        return BoltError::SUCCESS;
    }

    // Private helper to write a data chunk (header + payload)
    BoltError ChunkedWriter::write_chunk(const uint8_t* data, uint16_t size) {
        // This function is internal. `write_message` ensures size <= MAX_CHUNK_PAYLOAD_SIZE.
        // `has_error()` check is done by caller or at the start of `write_message`.

        BoltError err = write_chunk_header(size);
        if (err != BoltError::SUCCESS) {
            // error already set by write_chunk_header
            return err;
        }

        if (size > 0) {             // Only write payload if size is non-zero
            if (data == nullptr) {  // Should not happen if size > 0
                set_error(BoltError::INVALID_ARGUMENT);
                return last_error_;
            }
            if (stream_.fail()) {
                set_error(BoltError::NETWORK_ERROR);
                return last_error_;
            }
            stream_.write(reinterpret_cast<const char*>(data), size);
            if (stream_.fail()) {
                set_error(BoltError::NETWORK_ERROR);
                return BoltError::NETWORK_ERROR;
            }
        }
        return BoltError::SUCCESS;
    }

    // Private helper to write the end-of-message marker (a chunk with size 0)
    BoltError ChunkedWriter::write_end_of_message_marker() {
        return write_chunk_header(0);  // This writes a 2-byte header with 0x0000
    }

    // Public method to write a full message, chunked.
    BoltError ChunkedWriter::write_message(const std::vector<uint8_t>& message_data) {
        if (has_error()) return last_error_;  // Check if writer is already in an error state
        last_error_ = BoltError::SUCCESS;     // Reset error for this new operation

        const uint8_t* data_ptr = message_data.data();
        size_t total_message_size = message_data.size();
        size_t remaining_size = total_message_size;

        if (stream_.fail()) {  // Check stream before starting writes for this message
            set_error(BoltError::NETWORK_ERROR);
            return last_error_;
        }

        // Handle empty message payload: send a single EOM marker if message_data is empty.
        // Bolt messages are typically not empty (even GOODBYE has a PSS structure).
        // If message_data is empty, it means the PSS serialization resulted in zero bytes.
        // This case is unusual. A common interpretation would be to send *no* data chunks,
        // followed by the EOM marker.
        if (total_message_size == 0) {
            BoltError eom_err = write_end_of_message_marker();
            if (eom_err == BoltError::SUCCESS) {
                stream_.flush();  // Attempt to flush
                if (stream_.fail()) {
                    set_error(BoltError::NETWORK_ERROR);
                    return last_error_;
                }
            }
            return eom_err;  // Return error from writing EOM or SUCCESS
        }

        // Write data chunks
        while (remaining_size > 0) {
            uint16_t current_chunk_payload_size = static_cast<uint16_t>(std::min(remaining_size, static_cast<size_t>(MAX_CHUNK_PAYLOAD_SIZE)));

            BoltError chunk_write_err = write_chunk(data_ptr, current_chunk_payload_size);
            if (chunk_write_err != BoltError::SUCCESS) {
                // Error (and last_error_) already set by write_chunk or write_chunk_header
                return last_error_;
            }

            data_ptr += current_chunk_payload_size;
            remaining_size -= current_chunk_payload_size;
        }

        // After all data chunks are written, write the end-of-message marker
        BoltError eom_err = write_end_of_message_marker();
        if (eom_err != BoltError::SUCCESS) {
            return last_error_;  // Error already set
        }

        // Attempt to flush the stream to ensure all data is sent
        stream_.flush();
        if (stream_.fail()) {
            set_error(BoltError::NETWORK_ERROR);
            return last_error_;
        }

        return BoltError::SUCCESS;
    }

    // --- ChunkedReader Implementation ---

    ChunkedReader::ChunkedReader(std::istream& stream) : stream_(stream), last_error_(BoltError::SUCCESS) {
        if (stream_.fail()) {
            set_error(BoltError::INVALID_ARGUMENT);  // Or NETWORK_ERROR
        }
    }

    void ChunkedReader::set_error(BoltError err) {
        if (last_error_ == BoltError::SUCCESS && err != BoltError::SUCCESS) {
            last_error_ = err;
        }
    }

    // Private helper to read the 2-byte chunk header
    BoltError ChunkedReader::read_chunk_header(uint16_t& out_chunk_payload_size) {
        out_chunk_payload_size = 0;  // Initialize output
        if (has_error()) return last_error_;

        uint16_t size_be;  // To store big-endian size from stream

        if (stream_.fail()) {
            set_error(BoltError::NETWORK_ERROR);
            return last_error_;
        }
        stream_.read(reinterpret_cast<char*>(&size_be), sizeof(size_be));

        if (stream_.fail()) {                     // Check failbit (which is set on EOF by read if not enough bytes)
            set_error(BoltError::NETWORK_ERROR);  // Could be EOF if connection closed cleanly mid-message
            return last_error_;
        }
        if (static_cast<size_t>(stream_.gcount()) != sizeof(size_be)) {
            // This case should ideally be caught by stream.fail() if not enough bytes were read.
            // But as a safeguard:
            set_error(BoltError::NETWORK_ERROR);  // Incomplete read for header
            return last_error_;
        }

        out_chunk_payload_size = detail::be_to_host(size_be);

        // As per spec, MAX_CHUNK_PAYLOAD_SIZE is 65535 (0xFFFF).
        // The chunk_payload_size read from header can be this max value.
        // There's no separate check for > MAX_CHUNK_PAYLOAD_SIZE here, as uint16_t naturally holds up to this.
        // A CHUNK_TOO_LARGE error would be if a spec defined a lower practical limit, but Bolt uses full uint16_t range.

        return BoltError::SUCCESS;
    }

    // Private helper to read the payload of a chunk
    BoltError ChunkedReader::read_chunk_payload(uint16_t payload_size, std::vector<uint8_t>& buffer_to_append_to) {
        if (has_error()) return last_error_;
        if (payload_size == 0) return BoltError::SUCCESS;  // Nothing to read

        size_t current_buffer_capacity = buffer_to_append_to.capacity();
        size_t current_buffer_size = buffer_to_append_to.size();
        size_t required_capacity = current_buffer_size + payload_size;

        // Grow buffer if needed. This is where std::bad_alloc can occur.
        if (required_capacity > current_buffer_capacity) {
            try {
                // Reserve to avoid multiple small reallocations if reading many small chunks.
                // A growth factor could be used (e.g., 1.5x or 2x) for efficiency.
                // For simplicity here, just reserve what's immediately needed for this chunk.
                buffer_to_append_to.reserve(required_capacity);
            } catch (const std::bad_alloc&) {
                set_error(BoltError::OUT_OF_MEMORY);
                return last_error_;
            } catch (const std::length_error&) {          // If required_capacity is too large for vector
                set_error(BoltError::MESSAGE_TOO_LARGE);  // Or OUT_OF_MEMORY if more appropriate
                return last_error_;
            } catch (const std::exception&) {
                set_error(BoltError::UNKNOWN_ERROR);
                return last_error_;
            }
        }

        // Resize to exact size needed for appending, then read directly into new space.
        // This is less efficient than reading into a temporary buffer and then appending,
        // but simpler for now. A more optimized version might use a fixed-size read buffer.
        // We need to append, so we must first store current_buffer_size, then resize, then read.
        try {
            buffer_to_append_to.resize(required_capacity);  // Resize to make space
        } catch (const std::bad_alloc&) {
            set_error(BoltError::OUT_OF_MEMORY);
            return last_error_;
        } catch (const std::length_error&) {
            set_error(BoltError::MESSAGE_TOO_LARGE);
            return last_error_;
        } catch (const std::exception&) {
            set_error(BoltError::UNKNOWN_ERROR);
            return last_error_;
        }

        if (stream_.fail()) {
            set_error(BoltError::NETWORK_ERROR);
            return last_error_;
        }
        stream_.read(reinterpret_cast<char*>(buffer_to_append_to.data() + current_buffer_size), payload_size);

        if (stream_.fail()) {
            buffer_to_append_to.resize(current_buffer_size);  // Revert resize on partial read/failure
            set_error(BoltError::NETWORK_ERROR);
            return last_error_;
        }
        if (static_cast<size_t>(stream_.gcount()) != payload_size) {
            buffer_to_append_to.resize(current_buffer_size);  // Revert resize
            set_error(BoltError::NETWORK_ERROR);              // Incomplete read for payload
            return last_error_;
        }
        return BoltError::SUCCESS;
    }

    // Public method to read a full message, de-chunked.
    BoltError ChunkedReader::read_message(std::vector<uint8_t>& out_message_data) {
        if (has_error()) return last_error_;
        last_error_ = BoltError::SUCCESS;  // Reset error for this new operation

        out_message_data.clear();  // Ensure output vector starts empty for this message
        // Optionally, reserve a typical message size if known, e.g., out_message_data.reserve(4096);
        // This needs a try-catch for bad_alloc if done.

        uint16_t current_chunk_payload_size;
        BoltError err;

        while (true) {  // Loop to read chunks until EOM
            err = read_chunk_header(current_chunk_payload_size);
            if (err != BoltError::SUCCESS) {
                out_message_data.clear();  // Clear potentially partial data on error
                // last_error_ already set by read_chunk_header
                return last_error_;
            }

            if (current_chunk_payload_size == 0) {  // End-of-message marker (0x0000 chunk size)
                break;                              // Successfully read all chunks for this message
            }

            // Optional: Check against a max total message size to prevent OOM from malicious server
            // Example: constexpr size_t MAX_ALLOWED_TOTAL_MESSAGE_SIZE = 16 * 1024 * 1024; // 16MB
            // if (out_message_data.size() + current_chunk_payload_size > MAX_ALLOWED_TOTAL_MESSAGE_SIZE) {
            //    set_error(BoltError::MESSAGE_TOO_LARGE);
            //    out_message_data.clear();
            //    return last_error_;
            // }

            err = read_chunk_payload(current_chunk_payload_size, out_message_data);
            if (err != BoltError::SUCCESS) {
                out_message_data.clear();  // Clear potentially partial data on error
                // last_error_ already set by read_chunk_payload
                return last_error_;
            }
        }

        return BoltError::SUCCESS;
    }

}  // namespace boltprotocol