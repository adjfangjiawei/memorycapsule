#include "neo4j_bolt_transport/bolt_record.h"

namespace neo4j_bolt_transport {

    BoltRecord::BoltRecord(std::vector<boltprotocol::Value>&& fields_data, std::shared_ptr<const std::vector<std::string>> names_ptr) : fields_(std::move(fields_data)), field_names_ptr_(std::move(names_ptr)) {
    }

    std::pair<boltprotocol::BoltError, boltprotocol::Value> BoltRecord::get(size_t index) const {
        if (index >= fields_.size()) {
            return {boltprotocol::BoltError::INVALID_ARGUMENT, nullptr};  // Or a more specific "IndexOutOfBounds"
        }
        // Return a copy of the Value, or handle lifetime if Value contains non-copyable shared_ptrs carefully.
        // std::variant copy semantics should handle this correctly.
        return {boltprotocol::BoltError::SUCCESS, fields_[index]};
    }

    std::pair<boltprotocol::BoltError, boltprotocol::Value> BoltRecord::get(const std::string& field_name) const {
        if (!field_names_ptr_ || field_names_ptr_->empty()) {
            return {boltprotocol::BoltError::INVALID_ARGUMENT, nullptr};  // No field names available
        }
        // Could optimize with a cached map if records are accessed by name frequently
        for (size_t i = 0; i < field_names_ptr_->size(); ++i) {
            if ((*field_names_ptr_)[i] == field_name) {
                if (i < fields_.size()) {  // Should always be true if names match fields
                    return {boltprotocol::BoltError::SUCCESS, fields_[i]};
                } else {
                    // This indicates an internal inconsistency (more names than fields)
                    return {boltprotocol::BoltError::UNKNOWN_ERROR, nullptr};
                }
            }
        }
        return {boltprotocol::BoltError::INVALID_ARGUMENT, nullptr};  // Field name not found
    }

    const std::vector<std::string>& BoltRecord::field_names() const {
        static const std::vector<std::string> empty_field_names;  // For returning if no names ptr
        if (field_names_ptr_) {
            return *field_names_ptr_;
        }
        return empty_field_names;
    }

    // Template method implementations are in the header due to template instantiation.

}  // namespace neo4j_bolt_transport