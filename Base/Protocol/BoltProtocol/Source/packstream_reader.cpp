#include "boltprotocol/packstream_reader.h"  // 主声明

#include <cstring>    // For memcpy in consume_bytes (if used for non-trivial types, though here it's for raw bytes)
#include <exception>  // For std::bad_alloc (relevant for Value assignment in read() if target type allocates)
#include <iostream>   // For std::istream operations
// byte_order_utils.h is included via packstream_reader.h

namespace boltprotocol {

    // --- PackStreamReader Constructor and Basic IO ---

    PackStreamReader::PackStreamReader(const std::vector<uint8_t>& buffer) : buffer_ptr_(&buffer), stream_ptr_(nullptr), buffer_pos_(0), error_state_(BoltError::SUCCESS), current_recursion_depth_(0) {
        // No body needed if all initialization is in member initializer list
    }

    PackStreamReader::PackStreamReader(std::istream& stream) : buffer_ptr_(nullptr), stream_ptr_(&stream), buffer_pos_(0), error_state_(BoltError::SUCCESS), current_recursion_depth_(0) {
        if (!stream_ptr_ || stream_ptr_->fail()) {   // Basic stream validity check
            set_error(BoltError::INVALID_ARGUMENT);  // Or NETWORK_ERROR if appropriate
        }
    }

    void PackStreamReader::set_error(BoltError error) {
        if (error_state_ == BoltError::SUCCESS && error != BoltError::SUCCESS) {
            error_state_ = error;
        }
    }

    bool PackStreamReader::eof() const {
        if (has_error()) return true;  // If already in error, considered EOF for reading purposes
        if (buffer_ptr_) {
            return buffer_pos_ >= buffer_ptr_->size();
        }
        if (stream_ptr_) {
            // stream.eof() is only true after an attempt to read past EOF.
            // stream.peek() == EOF is a more reliable way to check without consuming.
            return stream_ptr_->peek() == EOF;
        }
        return true;  // No valid source
    }

    BoltError PackStreamReader::peek_byte(uint8_t& out_byte) {
        if (has_error()) return error_state_;
        out_byte = 0;  // Initialize

        if (buffer_ptr_) {
            if (buffer_pos_ < buffer_ptr_->size()) {
                out_byte = (*buffer_ptr_)[buffer_pos_];
                return BoltError::SUCCESS;
            }
            set_error(BoltError::DESERIALIZATION_ERROR);  // EOF on buffer
            return error_state_;
        }
        if (stream_ptr_) {
            if (stream_ptr_->fail()) {  // Check stream state before peeking
                set_error(BoltError::NETWORK_ERROR);
                return error_state_;
            }
            int byte_read = stream_ptr_->peek();  // peek() returns int
            if (byte_read == EOF) {
                if (stream_ptr_->bad()) {  // badbit indicates a serious I/O error
                    set_error(BoltError::NETWORK_ERROR);
                } else {                                          // eofbit, or (eofbit and failbit if formatting error also occurred)
                    set_error(BoltError::DESERIALIZATION_ERROR);  // EOF encountered
                }
                return error_state_;
            }
            out_byte = static_cast<uint8_t>(byte_read);
            return BoltError::SUCCESS;
        }
        set_error(BoltError::INVALID_ARGUMENT);  // No input source
        return error_state_;
    }

    BoltError PackStreamReader::consume_byte(uint8_t& out_byte) {
        if (has_error()) return error_state_;
        out_byte = 0;  // Initialize

        if (buffer_ptr_) {
            if (buffer_pos_ < buffer_ptr_->size()) {
                out_byte = (*buffer_ptr_)[buffer_pos_++];
                return BoltError::SUCCESS;
            }
            set_error(BoltError::DESERIALIZATION_ERROR);  // EOF on buffer
            return error_state_;
        }
        if (stream_ptr_) {
            if (stream_ptr_->fail()) {
                set_error(BoltError::NETWORK_ERROR);
                return error_state_;
            }
            int byte_read = stream_ptr_->get();  // get() returns int
            if (byte_read == EOF) {
                if (stream_ptr_->bad()) {
                    set_error(BoltError::NETWORK_ERROR);
                } else {
                    set_error(BoltError::DESERIALIZATION_ERROR);  // EOF encountered
                }
                return error_state_;
            }
            out_byte = static_cast<uint8_t>(byte_read);
            return BoltError::SUCCESS;
        }
        set_error(BoltError::INVALID_ARGUMENT);  // No input source
        return error_state_;
    }

    BoltError PackStreamReader::consume_bytes(void* dest, size_t size) {
        if (has_error()) return error_state_;
        if (size == 0) return BoltError::SUCCESS;  // Nothing to read
        if (dest == nullptr && size > 0) {         // Should not happen with internal calls typically
            set_error(BoltError::INVALID_ARGUMENT);
            return error_state_;
        }

        if (buffer_ptr_) {
            if (buffer_pos_ + size <= buffer_ptr_->size()) {
                // Using reinterpret_cast for buffer_ptr_->data() is okay as it's uint8_t
                std::memcpy(dest, buffer_ptr_->data() + buffer_pos_, size);
                buffer_pos_ += size;
                return BoltError::SUCCESS;
            }
            set_error(BoltError::DESERIALIZATION_ERROR);  // Buffer read out of bounds
            return error_state_;
        }
        if (stream_ptr_) {
            if (stream_ptr_->fail()) {
                set_error(BoltError::NETWORK_ERROR);
                return error_state_;
            }
            stream_ptr_->read(static_cast<char*>(dest), static_cast<std::streamsize>(size));

            // Check if the read was successful and complete
            if (static_cast<size_t>(stream_ptr_->gcount()) != size) {
                // failbit will be set if gcount() < size and not EOF, or badbit is set.
                // eofbit will be set if EOF was reached during the read.
                if (stream_ptr_->bad()) {
                    set_error(BoltError::NETWORK_ERROR);
                } else {                                          // Could be failbit (less data than requested) or eofbit (hit EOF)
                    set_error(BoltError::DESERIALIZATION_ERROR);  // Not enough data or other stream error
                }
                return error_state_;
            }
            return BoltError::SUCCESS;
        }
        set_error(BoltError::INVALID_ARGUMENT);  // No input source
        return error_state_;
    }

    // --- PackStreamReader Main Read Logic ---

    BoltError PackStreamReader::read(Value& out_value) {
        // Reset output value to a known state (nullptr for variant is a good default)
        try {
            out_value = nullptr;
        } catch (const std::bad_alloc&) {  // Should not happen for nullptr_t assignment
            set_error(BoltError::OUT_OF_MEMORY);
            return error_state_;
        }

        if (has_error()) return error_state_;
        if (eof()) {                                      // Check before attempting to consume any byte
            set_error(BoltError::DESERIALIZATION_ERROR);  // Attempt to read past EOF
            return error_state_;
        }

        uint8_t marker;
        BoltError err = consume_byte(marker);
        if (err != BoltError::SUCCESS) return err;  // error_state_ already set by consume_byte

        // Handle Tiny Positive Int (0 to 127) and Tiny Negative Int (-16 to -1) directly
        // These are outside the typical marker switch for performance and clarity.
        if (marker <= 0x7F) {  // Tiny Positive Int (0 to 127)
            try {
                out_value = static_cast<int64_t>(marker);
            } catch (const std::bad_alloc&) {  // Value variant assignment might allocate.
                set_error(BoltError::OUT_OF_MEMORY);
                return error_state_;
            }
            return BoltError::SUCCESS;
        }
        if (marker >= 0xF0) {  // Tiny Negative Int (-16 to -1)
            try {
                out_value = static_cast<int64_t>(static_cast<int8_t>(marker));
            } catch (const std::bad_alloc&) {
                set_error(BoltError::OUT_OF_MEMORY);
                return error_state_;
            }
            return BoltError::SUCCESS;
        }

        // Dispatch to type-specific handlers based on marker
        // These handlers are now in separate .cpp files but are still part of PackStreamReader class.
        switch (marker) {
            case MARKER_NULL:
                return read_null_value(out_value);
            case MARKER_FALSE:
                return read_boolean_value(false, out_value);
            case MARKER_TRUE:
                return read_boolean_value(true, out_value);
            case MARKER_FLOAT64:
                return read_float64_value(out_value);

            case MARKER_INT_8:
            case MARKER_INT_16:
            case MARKER_INT_32:
            case MARKER_INT_64:
                return read_integer_value(marker, out_value);

            // Ranged cases for C++20 and later (as per project requirement)
            case MARKER_TINY_STRING_BASE ...(MARKER_TINY_STRING_BASE + 0x0F):  // 0x80 to 0x8F
            case MARKER_STRING_8:
            case MARKER_STRING_16:
            case MARKER_STRING_32:
                return read_string_value(marker, out_value);

            case MARKER_TINY_LIST_BASE ...(MARKER_TINY_LIST_BASE + 0x0F):  // 0x90 to 0x9F
            case MARKER_LIST_8:
            case MARKER_LIST_16:
            case MARKER_LIST_32:
                return read_list_value(marker, out_value);

            case MARKER_TINY_MAP_BASE ...(MARKER_TINY_MAP_BASE + 0x0F):  // 0xA0 to 0xAF
            case MARKER_MAP_8:
            case MARKER_MAP_16:
            case MARKER_MAP_32:
                return read_map_value(marker, out_value);

            case MARKER_TINY_STRUCT_BASE ...(MARKER_TINY_STRUCT_BASE + 0x0F):  // 0xB0 to 0xBF
            case MARKER_STRUCT_8:
            case MARKER_STRUCT_16:
                return read_struct_value(marker, out_value);

            default:
                set_error(BoltError::INVALID_MESSAGE_FORMAT);  // Unknown marker
                return error_state_;
        }
    }

}  // namespace boltprotocol