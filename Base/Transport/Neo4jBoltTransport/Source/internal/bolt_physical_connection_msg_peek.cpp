#include "boltprotocol/packstream_reader.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::_peek_message_tag(const std::vector<uint8_t>& payload, boltprotocol::MessageTag& out_tag) const {
            if (payload.empty()) {
                if (logger_) logger_->warn("[ConnMsgPeek {}] Peek attempt on empty payload.", id_);
                return boltprotocol::BoltError::INVALID_MESSAGE_FORMAT;
            }
            // 使用临时的 PackStreamReader 来安全地读取消息头信息
            // PackStreamReader 的构造函数接受 const std::vector<uint8_t>&
            boltprotocol::PackStreamReader temp_reader(payload);
            uint8_t raw_tag_byte = 0;
            uint32_t num_fields = 0;  // peek_message_structure_header 会填充这个，即使我们这里不用

            // 调用 boltprotocol 命名空间下的自由函数
            boltprotocol::BoltError peek_err = boltprotocol::peek_message_structure_header(temp_reader, raw_tag_byte, num_fields);

            if (peek_err != boltprotocol::BoltError::SUCCESS) {
                if (logger_) logger_->warn("[ConnMsgPeek {}] Failed to peek message structure header: {}", id_, static_cast<int>(peek_err));
                return peek_err;
            }
            out_tag = static_cast<boltprotocol::MessageTag>(raw_tag_byte);
            return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport