#ifndef BOLT_PROTOCOL_IMPL_DETAIL_BYTE_ORDER_UTILS_H
#define BOLT_PROTOCOL_IMPL_DETAIL_BYTE_ORDER_UTILS_H

#include <algorithm>  // For std::reverse
#include <bit>        // For std::endian (C++20 and later)
#include <cstdint>
#include <type_traits>  // For std::is_integral_v, std::is_enum_v

namespace boltprotocol {
    namespace detail {

        // Helper to swap bytes of an integer type T
        template <typename T>
        inline T swap_bytes_helper(T value) {
            static_assert(std::is_integral_v<T> || std::is_enum_v<T>, "swap_bytes_helper requires an integral or enum type.");
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            auto* bytes = reinterpret_cast<unsigned char*>(&value);
            std::reverse(bytes, bytes + sizeof(T));
            return value;
        }

        // --- Host to Big Endian (Network Order) ---
        inline uint16_t host_to_be(uint16_t val) {
            if constexpr (std::endian::native == std::endian::little) {
                return swap_bytes_helper(val);
            } else {  // std::endian::big (or other, assuming network order is what we want if not little)
                return val;
            }
        }

        inline uint32_t host_to_be(uint32_t val) {
            if constexpr (std::endian::native == std::endian::little) {
                return swap_bytes_helper(val);
            } else {
                return val;
            }
        }

        inline uint64_t host_to_be(uint64_t val) {
            if constexpr (std::endian::native == std::endian::little) {
                return swap_bytes_helper(val);
            } else {
                return val;
            }
        }

        // --- Big Endian (Network Order) to Host ---
        inline uint16_t be_to_host(uint16_t val_be) {
            if constexpr (std::endian::native == std::endian::little) {
                return swap_bytes_helper(val_be);
            } else {
                return val_be;
            }
        }

        inline uint32_t be_to_host(uint32_t val_be) {
            if constexpr (std::endian::native == std::endian::little) {
                return swap_bytes_helper(val_be);
            } else {
                return val_be;
            }
        }

        inline uint64_t be_to_host(uint64_t val_be) {
            if constexpr (std::endian::native == std::endian::little) {
                return swap_bytes_helper(val_be);
            } else {
                return val_be;
            }
        }

    }  // namespace detail
}  // namespace boltprotocol

#endif  // BOLT_PROTOCOL_IMPL_DETAIL_BYTE_ORDER_UTILS_H