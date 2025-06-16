#ifndef BOLTPROTOCOL_HANDSHAKE_H
#define BOLTPROTOCOL_HANDSHAKE_H

#include <array>
#include <cstdint>
#include <iosfwd>  // For std::istream, std::ostream
#include <vector>

#include "boltprotocol/message_defs.h"  // For BoltError, versions::Version

namespace boltprotocol {

    // Magic Preamble 已经在 message_defs.h 中定义
    // constexpr uint32_t BOLT_MAGIC_PREAMBLE = 0x6060B017;

    constexpr size_t HANDSHAKE_NUM_PROPOSED_VERSIONS = 4;
    constexpr size_t HANDSHAKE_VERSION_SIZE_BYTES = 4;
    constexpr size_t HANDSHAKE_REQUEST_SIZE_BYTES = sizeof(BOLT_MAGIC_PREAMBLE) + (HANDSHAKE_NUM_PROPOSED_VERSIONS * HANDSHAKE_VERSION_SIZE_BYTES);
    constexpr size_t HANDSHAKE_RESPONSE_SIZE_BYTES = HANDSHAKE_VERSION_SIZE_BYTES;

    /**
     * @brief 构建 Bolt 握手请求字节串。
     *
     * @param proposed_versions 一个包含最多4个提议版本的向量。如果少于4个，将使用0填充。
     *                          如果多于4个，只使用前4个。版本应按偏好顺序排列。
     * @param out_handshake_bytes 输出参数，用于存储生成的20字节握手请求。
     * @return BoltError::SUCCESS 如果成功。
     *         BoltError::INVALID_ARGUMENT 如果 proposed_versions 为空。
     */
    BoltError build_handshake_request(const std::vector<versions::Version>& proposed_versions, std::array<uint8_t, HANDSHAKE_REQUEST_SIZE_BYTES>& out_handshake_bytes);

    /**
     * @brief 解析服务器的 Bolt 握手响应。
     *
     * @param server_response 服务器返回的4字节响应。
     * @param out_negotiated_version 输出参数，用于存储服务器选择的版本。
     * @return BoltError::SUCCESS 如果成功解析出一个有效版本。
     *         BoltError::HANDSHAKE_NO_COMMON_VERSION 如果服务器返回全零，表示不支持任何提议版本。
     *         BoltError::DESERIALIZATION_ERROR 如果响应格式无效（虽然对于4字节很简单）。
     */
    BoltError parse_handshake_response(const std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES>& server_response, versions::Version& out_negotiated_version);

    /**
     * @brief 执行完整的 Bolt 握手过程 (发送请求并接收/解析响应)。
     *        注意: 此函数仅处理握手字节的交换和解析，不处理网络连接的建立和关闭。
     *
     * @param ostream 用于发送握手请求的输出流。
     * @param istream 用于接收握手响应的输入流。
     * @param proposed_versions 客户端提议的协议版本列表。
     * @param out_negotiated_version 输出参数，存储服务器选择的协议版本。
     * @return BoltError::SUCCESS 如果握手成功。
     *         BoltError::NETWORK_ERROR 如果发生流读写错误。
     *         BoltError::INVALID_ARGUMENT 如果 proposed_versions 为空。
     *         BoltError::HANDSHAKE_NO_COMMON_VERSION 如果服务器拒绝所有版本。
     *         或其他相关错误码。
     */
    BoltError perform_handshake(std::ostream& ostream, std::istream& istream, const std::vector<versions::Version>& proposed_versions, versions::Version& out_negotiated_version);

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_HANDSHAKE_H