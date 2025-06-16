#ifndef BOLT_PROTOCOL_IMPL_PACKSTREAM_WRITER_H
#define BOLT_PROTOCOL_IMPL_PACKSTREAM_WRITER_H

#include <cstdint>
#include <iosfwd>
#include <memory>  // For std::shared_ptr (used in Value)
#include <string>
#include <type_traits>
#include <vector>

#include "boltprotocol/detail/byte_order_utils.h"
#include "boltprotocol/message_defs.h"  // Includes Value, BoltError
#include "boltprotocol/packstream_constants.h"

namespace boltprotocol {

    class PackStreamWriter {
      public:
        explicit PackStreamWriter(std::vector<uint8_t>& buffer);
        explicit PackStreamWriter(std::ostream& stream);

        // 禁止拷贝和移动
        PackStreamWriter(const PackStreamWriter&) = delete;
        PackStreamWriter& operator=(const PackStreamWriter&) = delete;
        PackStreamWriter(PackStreamWriter&&) = delete;
        PackStreamWriter& operator=(PackStreamWriter&&) = delete;

        /**
         * @brief Writes a single PackStream Value to the output.
         * @param value The Value to serialize and write.
         * @return BoltError::SUCCESS on successful write.
         *         BoltError::SERIALIZATION_ERROR for logical errors (e.g., string too long).
         *         BoltError::NETWORK_ERROR for underlying stream errors.
         *         BoltError::INVALID_ARGUMENT if writer not properly initialized.
         *         BoltError::OUT_OF_MEMORY if a memory allocation fails (e.g. vector resize).
         */
        BoltError write(const Value& value);

        bool has_error() const {
            return error_state_ != BoltError::SUCCESS;
        }
        BoltError get_error() const {
            return error_state_;
        }

      private:
        // 底层IO辅助函数, 它们会设置 error_state_
        BoltError append_byte(uint8_t byte);
        BoltError append_bytes(const void* data, size_t size);

        template <typename T>
        BoltError append_network_int(T value) {
            static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "append_network_int requires an integral or enum type.");
            if (has_error()) return error_state_;

            BoltError err;
            if constexpr (sizeof(T) == 1) {
                err = append_byte(static_cast<uint8_t>(value));
            } else if constexpr (sizeof(T) == 2) {
                uint16_t be_val = detail::host_to_be(static_cast<uint16_t>(value));
                err = append_bytes(&be_val, sizeof(be_val));
            } else if constexpr (sizeof(T) == 4) {
                uint32_t be_val = detail::host_to_be(static_cast<uint32_t>(value));
                err = append_bytes(&be_val, sizeof(be_val));
            } else if constexpr (sizeof(T) == 8) {
                uint64_t be_val = detail::host_to_be(static_cast<uint64_t>(value));
                err = append_bytes(&be_val, sizeof(be_val));
            } else {
                static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "Unsupported integer size for append_network_int.");
                set_error(BoltError::INVALID_ARGUMENT);  // 理论上会被 static_assert 捕获
                return BoltError::INVALID_ARGUMENT;
            }
            return err;  // 返回 append_byte 或 append_bytes 的结果
        }

      public:
        void set_error(BoltError error);

      private:
        // 类型特定的内部写入函数 (现在返回 BoltError)
        BoltError write_null_internal();
        BoltError write_boolean_internal(bool bool_value);
        BoltError write_integer_internal(int64_t int_value);
        BoltError write_float_internal(double float_value);
        BoltError write_string_header_internal(uint32_t size);
        BoltError write_string_data_internal(const std::string& value_str);  // 已改为 const ref
        BoltError serialize_string_internal(const std::string& str_value);
        BoltError write_list_header_internal(uint32_t size);
        BoltError serialize_list_internal(const BoltList& list_data);  // 已改为 const ref
        BoltError write_map_header_internal(uint32_t size);
        BoltError serialize_map_internal(const BoltMap& map_data);  // 已改为 const ref
        BoltError write_struct_header_internal(uint8_t tag, uint32_t size);
        BoltError serialize_structure_internal(const PackStreamStructure& struct_data);  // 已改为 const ref

        std::vector<uint8_t>* buffer_ptr_ = nullptr;
        std::ostream* stream_ptr_ = nullptr;
        BoltError error_state_ = BoltError::SUCCESS;

        // 递归深度计数器
        static constexpr uint32_t MAX_RECURSION_DEPTH = 100;  // 与 Reader 保持一致
        uint32_t current_recursion_depth_ = 0;
    };

}  // namespace boltprotocol

#endif  // BOLT_PROTOCOL_IMPL_PACKSTREAM_WRITER_H