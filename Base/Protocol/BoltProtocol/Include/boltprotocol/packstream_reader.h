#ifndef BOLT_PROTOCOL_IMPL_PACKSTREAM_READER_H
#define BOLT_PROTOCOL_IMPL_PACKSTREAM_READER_H

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

    class PackStreamReader {
      public:
        explicit PackStreamReader(const std::vector<uint8_t>& buffer);
        explicit PackStreamReader(std::istream& stream);

        // 禁止拷贝和移动以避免对内部状态和流/缓冲区的复杂管理
        PackStreamReader(const PackStreamReader&) = delete;
        PackStreamReader& operator=(const PackStreamReader&) = delete;
        PackStreamReader(PackStreamReader&&) = delete;
        PackStreamReader& operator=(PackStreamReader&&) = delete;

        /**
         * @brief Reads a single PackStream Value from the input.
         * @param out_value Output parameter where the read Value will be stored if successful.
         *                  Its content is undefined if an error occurs.
         * @return BoltError::SUCCESS on successful read.
         *         BoltError::DESERIALIZATION_ERROR for format errors, unexpected EOF, etc.
         *         BoltError::NETWORK_ERROR for underlying stream errors.
         *         BoltError::INVALID_ARGUMENT if reader not properly initialized.
         *         BoltError::OUT_OF_MEMORY if a memory allocation fails.
         */
        BoltError read(Value& out_value);

        bool has_error() const {
            return error_state_ != BoltError::SUCCESS;
        }
        BoltError get_error() const {
            return error_state_;
        }

        /**
         * @brief Checks if the end of the underlying buffer or stream has been reached.
         *        Also returns true if an error has occurred, as further reading is not possible.
         * @return True if EOF or error, false otherwise.
         */
        bool eof() const;

      private:
        // 底层IO辅助函数, 它们会设置 error_state_
        BoltError peek_byte(uint8_t& out_byte);
        BoltError consume_byte(uint8_t& out_byte);
        BoltError consume_bytes(void* dest, size_t size);

        template <typename T>
        BoltError consume_network_int(T& out_val) {
            static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "consume_network_int requires an integral or enum type.");
            if (has_error()) return error_state_;  // 如果已处于错误状态，则不继续

            // 初始化 out_val 以防早期返回
            out_val = T{};
            BoltError err;

            if constexpr (sizeof(T) == 1) {
                uint8_t byte_val;
                err = consume_byte(byte_val);
                if (err != BoltError::SUCCESS) return err;
                out_val = static_cast<T>(byte_val);
                return BoltError::SUCCESS;
            } else {
                typename std::conditional<sizeof(T) == 2, uint16_t, typename std::conditional<sizeof(T) == 4, uint32_t, uint64_t>::type>::type raw_be_val = 0;

                err = consume_bytes(&raw_be_val, sizeof(raw_be_val));
                if (err != BoltError::SUCCESS) return err;

                if constexpr (sizeof(T) == 2) {
                    out_val = static_cast<T>(detail::be_to_host(static_cast<uint16_t>(raw_be_val)));
                } else if constexpr (sizeof(T) == 4) {
                    out_val = static_cast<T>(detail::be_to_host(static_cast<uint32_t>(raw_be_val)));
                } else if constexpr (sizeof(T) == 8) {
                    out_val = static_cast<T>(detail::be_to_host(static_cast<uint64_t>(raw_be_val)));
                } else {
                    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "Unsupported integer size for consume_network_int.");
                    set_error(BoltError::INVALID_ARGUMENT);  // 理论上会被 static_assert 捕获
                    return BoltError::INVALID_ARGUMENT;
                }
                return BoltError::SUCCESS;
            }
        }

      public:
        void set_error(BoltError error);

      private:
        // 类型特定的读取辅助函数 (现在返回 BoltError 并通过 out_value 输出参数返回结果)
        BoltError read_null_value(Value& out_value);
        BoltError read_boolean_value(bool bool_val_from_marker, Value& out_value);
        BoltError read_float64_value(Value& out_value);
        BoltError read_integer_value(uint8_t marker, Value& out_value);
        BoltError read_string_value(uint8_t marker, Value& out_value);
        BoltError read_string_data_into(std::string& out_string, uint32_t size);
        BoltError read_list_value(uint8_t marker, Value& out_value);
        BoltError read_list_elements_into(std::shared_ptr<BoltList>& list_sptr, uint32_t size);
        BoltError read_map_value(uint8_t marker, Value& out_value);
        BoltError read_map_pairs_into(std::shared_ptr<BoltMap>& map_sptr, uint32_t size);
        BoltError read_struct_value(uint8_t marker, Value& out_value);
        BoltError read_struct_fields_into(std::shared_ptr<PackStreamStructure>& struct_sptr, uint8_t tag, uint32_t size);

        const std::vector<uint8_t>* buffer_ptr_ = nullptr;
        std::istream* stream_ptr_ = nullptr;
        size_t buffer_pos_ = 0;
        BoltError error_state_ = BoltError::SUCCESS;

        // 递归深度计数器，用于防止解析恶意构造的数据时栈溢出
        // (在实际的 read_list/map/struct_elements_into 中使用)
        static constexpr uint32_t MAX_RECURSION_DEPTH = 100;  // 可配置
        uint32_t current_recursion_depth_ = 0;
    };

}  // namespace boltprotocol

#endif  // BOLT_PROTOCOL_IMPL_PACKSTREAM_READER_H