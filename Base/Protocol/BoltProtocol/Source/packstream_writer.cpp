#include "boltprotocol/packstream_writer.h"

#include <cstring>    // For memcpy
#include <exception>  // For std::bad_alloc, std::exception
#include <iostream>   // For std::ostream
#include <variant>    // For std::visit

namespace boltprotocol {

    // --- PackStreamWriter Constructor and Low-Level IO ---

    PackStreamWriter::PackStreamWriter(std::vector<uint8_t>& buffer) : buffer_ptr_(&buffer), stream_ptr_(nullptr), error_state_(BoltError::SUCCESS), current_recursion_depth_(0) {
    }

    PackStreamWriter::PackStreamWriter(std::ostream& stream) : stream_ptr_(&stream), buffer_ptr_(nullptr), error_state_(BoltError::SUCCESS), current_recursion_depth_(0) {
        if (!stream_ptr_ || stream_ptr_->fail()) {
            set_error(BoltError::INVALID_ARGUMENT);
        }
    }

    void PackStreamWriter::set_error(BoltError error) {
        if (error_state_ == BoltError::SUCCESS && error != BoltError::SUCCESS) {
            error_state_ = error;
        }
    }

    BoltError PackStreamWriter::append_byte(uint8_t byte) {
        if (has_error()) return error_state_;

        if (buffer_ptr_) {
            try {
                buffer_ptr_->push_back(byte);  // Potential std::bad_alloc
            } catch (const std::bad_alloc&) {
                set_error(BoltError::OUT_OF_MEMORY);
                return error_state_;
            } catch (const std::exception&) {  // Other possible exceptions from vector
                set_error(BoltError::UNKNOWN_ERROR);
                return error_state_;
            }
        } else if (stream_ptr_) {
            if (stream_ptr_->fail()) {  // Check before writing
                set_error(BoltError::NETWORK_ERROR);
                return error_state_;
            }
            stream_ptr_->put(static_cast<char>(byte));
            if (stream_ptr_->fail()) {
                set_error(BoltError::NETWORK_ERROR);
                return error_state_;
            }
        } else {
            set_error(BoltError::INVALID_ARGUMENT);  // No output target
            return error_state_;
        }
        return BoltError::SUCCESS;
    }

    BoltError PackStreamWriter::append_bytes(const void* data, size_t size) {
        if (has_error()) return error_state_;
        if (size == 0) return BoltError::SUCCESS;
        if (data == nullptr && size > 0) {
            set_error(BoltError::INVALID_ARGUMENT);
            return error_state_;
        }

        if (buffer_ptr_) {
            const auto* byte_data = static_cast<const uint8_t*>(data);
            try {
                buffer_ptr_->insert(buffer_ptr_->end(), byte_data, byte_data + size);  // Potential std::bad_alloc
            } catch (const std::bad_alloc&) {
                set_error(BoltError::OUT_OF_MEMORY);
                return error_state_;
            } catch (const std::exception&) {
                set_error(BoltError::UNKNOWN_ERROR);
                return error_state_;
            }
        } else if (stream_ptr_) {
            if (stream_ptr_->fail()) {  // Check before writing
                set_error(BoltError::NETWORK_ERROR);
                return error_state_;
            }
            stream_ptr_->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (stream_ptr_->fail()) {
                set_error(BoltError::NETWORK_ERROR);
                return error_state_;
            }
        } else {
            set_error(BoltError::INVALID_ARGUMENT);  // No output target
            return error_state_;
        }
        return BoltError::SUCCESS;
    }

    // --- PackStreamWriter Main Dispatch Logic ---

    BoltError PackStreamWriter::write(const Value& value) {
        if (has_error()) return error_state_;  // Already in error

        // Visitor lambda
        auto visitor = [&](const auto& arg) -> BoltError {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return write_null_internal();
            } else if constexpr (std::is_same_v<T, bool>) {
                return write_boolean_internal(arg);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return write_integer_internal(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                return write_float_internal(arg);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return serialize_string_internal(arg);
            } else if constexpr (std::is_same_v<T, std::shared_ptr<BoltList>>) {
                if (!arg) {  // Handle null shared_ptr as PackStream NULL
                    return write_null_internal();
                }
                return serialize_list_internal(*arg);
            } else if constexpr (std::is_same_v<T, std::shared_ptr<BoltMap>>) {
                if (!arg) {
                    return write_null_internal();
                }
                return serialize_map_internal(*arg);
            } else if constexpr (std::is_same_v<T, std::shared_ptr<PackStreamStructure>>) {
                if (!arg) {
                    return write_null_internal();
                }
                return serialize_structure_internal(*arg);
            } else {
                // Should not be reached if Value variant is exhaustive for PackStream types
                set_error(BoltError::SERIALIZATION_ERROR);
                return error_state_;
            }
        };

        return std::visit(visitor, value);
    }

}  // namespace boltprotocol