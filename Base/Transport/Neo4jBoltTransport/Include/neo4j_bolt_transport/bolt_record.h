#ifndef NEO4J_BOLT_TRANSPORT_BOLT_RECORD_H
#define NEO4J_BOLT_TRANSPORT_BOLT_RECORD_H

#include <map>
#include <memory>  // For std::shared_ptr for field_names
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/bolt_structure_serialization.h"
#include "boltprotocol/bolt_structure_types.h"
#include "boltprotocol/message_defs.h"

namespace neo4j_bolt_transport {

    class BoltRecord {
      public:
        // Constructor takes rvalue refs to move data if possible.
        // field_names_ptr is shared because multiple records from the same result stream share it.
        BoltRecord(std::vector<boltprotocol::Value>&& fields_data, std::shared_ptr<const std::vector<std::string>> field_names_ptr);

        BoltRecord(const BoltRecord&) = delete;  // Typically records are not copied once created
        BoltRecord& operator=(const BoltRecord&) = delete;
        BoltRecord(BoltRecord&&) noexcept = default;
        BoltRecord& operator=(BoltRecord&&) noexcept = default;

        // Access by index
        std::pair<boltprotocol::BoltError, boltprotocol::Value> get(size_t index) const;

        // Access by name
        std::pair<boltprotocol::BoltError, boltprotocol::Value> get(const std::string& field_name) const;

        // Typed access by index
        template <typename T>
        std::pair<boltprotocol::BoltError, T> get_as(size_t index) const;

        // Typed access by name
        template <typename T>
        std::pair<boltprotocol::BoltError, T> get_as(const std::string& field_name) const;

        // Typed access for Bolt Structures (Node, Relationship, etc.)
        // These require the Bolt version for correct deserialization of version-dependent fields.
        template <typename T_BoltStructure>
        std::pair<boltprotocol::BoltError, T_BoltStructure> get_bolt_structure_as(size_t index,
                                                                                  const boltprotocol::versions::Version& bolt_version,
                                                                                  bool utc_patch_active_for_4_4 = false  // Relevant for DateTime types in Bolt 4.4
        ) const;

        template <typename T_BoltStructure>
        std::pair<boltprotocol::BoltError, T_BoltStructure> get_bolt_structure_as(const std::string& field_name, const boltprotocol::versions::Version& bolt_version, bool utc_patch_active_for_4_4 = false) const;

        size_t field_count() const noexcept {
            return fields_.size();
        }
        const std::vector<std::string>& field_names() const;  // Returns empty if no names available

      private:
        std::vector<boltprotocol::Value> fields_;
        std::shared_ptr<const std::vector<std::string>> field_names_ptr_;  // Pointer to shared field names
        // Optional: Cache field name to index map for faster named lookups if records are long-lived
        // mutable std::optional<std::map<std::string, size_t>> field_name_to_index_cache_;
        // const std::map<std::string, size_t>& get_field_name_map() const;
    };

    // --- Template Implementations for BoltRecord ---
    template <typename T>
    std::pair<boltprotocol::BoltError, T> BoltRecord::get_as(size_t index) const {
        auto value_result = get(index);
        if (value_result.first != boltprotocol::BoltError::SUCCESS) {
            return {value_result.first, T{}};
        }
        if (std::holds_alternative<T>(value_result.second)) {
            try {
                return {boltprotocol::BoltError::SUCCESS, std::get<T>(value_result.second)};
            } catch (const std::bad_variant_access&) {                         // Should not happen if holds_alternative is true
                return {boltprotocol::BoltError::DESERIALIZATION_ERROR, T{}};  // Type mismatch
            }
        }
        // Special case for int64_t, allow conversion from other integral types if safe (e.g. int32_t -> int64_t)
        // This requires more complex logic or a dedicated conversion utility.
        // For now, strict type match.
        return {boltprotocol::BoltError::DESERIALIZATION_ERROR, T{}};  // Type mismatch
    }

    template <typename T>
    std::pair<boltprotocol::BoltError, T> BoltRecord::get_as(const std::string& field_name) const {
        if (!field_names_ptr_ || field_names_ptr_->empty()) {
            return {boltprotocol::BoltError::INVALID_ARGUMENT, T{}};  // No field names available
        }
        ptrdiff_t index = -1;
        for (size_t i = 0; i < field_names_ptr_->size(); ++i) {
            if ((*field_names_ptr_)[i] == field_name) {
                index = static_cast<ptrdiff_t>(i);
                break;
            }
        }
        if (index == -1) {
            return {boltprotocol::BoltError::INVALID_ARGUMENT, T{}};  // Field name not found
        }
        return get_as<T>(static_cast<size_t>(index));
    }

    template <typename T_BoltStructure>
    std::pair<boltprotocol::BoltError, T_BoltStructure> BoltRecord::get_bolt_structure_as(size_t index, const boltprotocol::versions::Version& bolt_version, bool utc_patch_active_for_4_4) const {
        auto value_result = get(index);
        if (value_result.first != boltprotocol::BoltError::SUCCESS) {
            return {value_result.first, T_BoltStructure{}};
        }

        T_BoltStructure typed_struct;
        boltprotocol::BoltError conversion_err;

        // Use the value_to_typed_struct that takes version and patch info
        if constexpr (std::is_same_v<T_BoltStructure, boltprotocol::BoltDateTime> || std::is_same_v<T_BoltStructure, boltprotocol::BoltDateTimeZoneId> || std::is_same_v<T_BoltStructure, boltprotocol::BoltNode> || std::is_same_v<T_BoltStructure, boltprotocol::BoltRelationship> ||
                      std::is_same_v<T_BoltStructure, boltprotocol::BoltUnboundRelationship> || std::is_same_v<T_BoltStructure, boltprotocol::BoltPath>) {
            conversion_err = boltprotocol::value_to_typed_struct(value_result.second, typed_struct, bolt_version, utc_patch_active_for_4_4);
        } else {  // For types like BoltDate, BoltTime that don't need version for deserialization
            conversion_err = boltprotocol::value_to_typed_struct(value_result.second, typed_struct);
        }

        if (conversion_err != boltprotocol::BoltError::SUCCESS) {
            return {conversion_err, T_BoltStructure{}};
        }
        return {boltprotocol::BoltError::SUCCESS, typed_struct};
    }

    template <typename T_BoltStructure>
    std::pair<boltprotocol::BoltError, T_BoltStructure> BoltRecord::get_bolt_structure_as(const std::string& field_name, const boltprotocol::versions::Version& bolt_version, bool utc_patch_active_for_4_4) const {
        if (!field_names_ptr_ || field_names_ptr_->empty()) {
            return {boltprotocol::BoltError::INVALID_ARGUMENT, T_BoltStructure{}};
        }
        ptrdiff_t index = -1;
        for (size_t i = 0; i < field_names_ptr_->size(); ++i) {
            if ((*field_names_ptr_)[i] == field_name) {
                index = static_cast<ptrdiff_t>(i);
                break;
            }
        }
        if (index == -1) {
            return {boltprotocol::BoltError::INVALID_ARGUMENT, T_BoltStructure{}};
        }
        return get_bolt_structure_as<T_BoltStructure>(static_cast<size_t>(index), bolt_version, utc_patch_active_for_4_4);
    }

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_BOLT_RECORD_H