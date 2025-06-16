#ifndef BOLTPROTOCOL_MESSAGE_TAGS_H
#define BOLTPROTOCOL_MESSAGE_TAGS_H

#include <cstdint>

namespace boltprotocol {

    enum class MessageTag : uint8_t {
        HELLO = 0x01,
        RUN = 0x10,
        DISCARD = 0x2F,
        PULL = 0x3F,
        BEGIN = 0x11,
        COMMIT = 0x12,
        ROLLBACK = 0x13,
        RESET = 0x0F,
        GOODBYE = 0x02,
        ROUTE = 0x66,
        TELEMETRY = 0x54,
        LOGON = 0x6A,
        LOGOFF = 0x6B,
        SUCCESS = 0x70,
        RECORD = 0x71,
        IGNORED = 0x7E,
        FAILURE = 0x7F,
        // INIT = 0x01, // Same as HELLO tag
        // ACK_FAILURE = 0x0E // Bolt v1/v2 only
    };

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_MESSAGE_TAGS_H