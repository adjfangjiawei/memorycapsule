#ifndef BOLT_PROTOCOL_IMPL_PACKSTREAM_CONSTANTS_H
#define BOLT_PROTOCOL_IMPL_PACKSTREAM_CONSTANTS_H

#include <cstdint>

namespace boltprotocol {

    // PackStream Marker Bytes
    constexpr uint8_t MARKER_NULL = 0xC0;
    constexpr uint8_t MARKER_FALSE = 0xC2;
    constexpr uint8_t MARKER_TRUE = 0xC3;
    constexpr uint8_t MARKER_FLOAT64 = 0xC1;

    // Integer markers
    // Tiny Int: -16 to 127 directly encoded in the marker byte
    // INT_8:  0xC8 <int8>
    // INT_16: 0xC9 <int16>
    // INT_32: 0xCA <int32>
    // INT_64: 0xCB <int64>
    constexpr uint8_t MARKER_INT_8 = 0xC8;
    constexpr uint8_t MARKER_INT_16 = 0xC9;
    constexpr uint8_t MARKER_INT_32 = 0xCA;
    constexpr uint8_t MARKER_INT_64 = 0xCB;

    // String markers
    // TINY_STRING: 0x80..0x8F (length 0-15)
    // STRING_8:    0xD0 <len_uint8> <utf8_bytes>
    // STRING_16:   0xD1 <len_uint16> <utf8_bytes>
    // STRING_32:   0xD2 <len_uint32> <utf8_bytes>
    constexpr uint8_t MARKER_TINY_STRING_BASE = 0x80;  // Base for 0x80 | len
    constexpr uint8_t MARKER_STRING_8 = 0xD0;
    constexpr uint8_t MARKER_STRING_16 = 0xD1;
    constexpr uint8_t MARKER_STRING_32 = 0xD2;

    // List markers
    // TINY_LIST:   0x90..0x9F (size 0-15)
    // LIST_8:      0xD4 <size_uint8>
    // LIST_16:     0xD5 <size_uint16>
    // LIST_32:     0xD6 <size_uint32>
    constexpr uint8_t MARKER_TINY_LIST_BASE = 0x90;  // Base for 0x90 | size
    constexpr uint8_t MARKER_LIST_8 = 0xD4;
    constexpr uint8_t MARKER_LIST_16 = 0xD5;
    constexpr uint8_t MARKER_LIST_32 = 0xD6;

    // Map markers
    // TINY_MAP:    0xA0..0xAF (size 0-15)
    // MAP_8:       0xD8 <size_uint8>
    // MAP_16:      0xD9 <size_uint16>
    // MAP_32:      0xDA <size_uint32>
    constexpr uint8_t MARKER_TINY_MAP_BASE = 0xA0;  // Base for 0xA0 | size
    constexpr uint8_t MARKER_MAP_8 = 0xD8;
    constexpr uint8_t MARKER_MAP_16 = 0xD9;
    constexpr uint8_t MARKER_MAP_32 = 0xDA;

    // Structure markers
    // TINY_STRUCT: 0xB0..0xBF (size 0-15) <tag_uint8>
    // STRUCT_8:    0xDC <size_uint8> <tag_uint8>
    // STRUCT_16:   0xDD <size_uint16> <tag_uint8>
    constexpr uint8_t MARKER_TINY_STRUCT_BASE = 0xB0;  // Base for 0xB0 | size
    constexpr uint8_t MARKER_STRUCT_8 = 0xDC;
    constexpr uint8_t MARKER_STRUCT_16 = 0xDD;

}  // namespace boltprotocol

#endif  // BOLT_PROTOCOL_IMPL_PACKSTREAM_CONSTANTS_H