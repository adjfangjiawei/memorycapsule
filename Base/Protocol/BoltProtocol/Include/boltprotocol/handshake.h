#ifndef BOLTPROTOCOL_HANDSHAKE_H
#define BOLTPROTOCOL_HANDSHAKE_H

#include <array>
#include <boost/asio/basic_socket_iostream.hpp>  // For boost::asio::basic_socket_iostream
#include <boost/asio/ip/tcp.hpp>                 // For boost::asio::ip::tcp
#include <boost/asio/read.hpp>
#include <boost/asio/ssl/stream.hpp>  // For boost::asio::ssl::stream
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>
#include <cstdint>
#include <cstring>  // For std::memcpy
#include <istream>  // For std::istream characteristics if needed
#include <ostream>  // For std::ostream characteristics if needed
#include <vector>

#include "boltprotocol/detail/byte_order_utils.h"
#include "boltprotocol/message_defs.h"

namespace boltprotocol {

    constexpr size_t HANDSHAKE_NUM_PROPOSED_VERSIONS = 4;
    constexpr size_t HANDSHAKE_VERSION_SIZE_BYTES = 4;
    constexpr size_t HANDSHAKE_REQUEST_SIZE_BYTES = sizeof(BOLT_MAGIC_PREAMBLE) + (HANDSHAKE_NUM_PROPOSED_VERSIONS * HANDSHAKE_VERSION_SIZE_BYTES);
    constexpr size_t HANDSHAKE_RESPONSE_SIZE_BYTES = HANDSHAKE_VERSION_SIZE_BYTES;

    BoltError build_handshake_request(const std::vector<versions::Version>& proposed_versions, std::array<uint8_t, HANDSHAKE_REQUEST_SIZE_BYTES>& out_handshake_bytes);
    BoltError parse_handshake_response(const std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES>& server_response, versions::Version& out_negotiated_version);

    template <typename StreamType>
    BoltError perform_handshake(StreamType& stream, const std::vector<versions::Version>& proposed_versions, versions::Version& out_negotiated_version) {
        std::array<uint8_t, HANDSHAKE_REQUEST_SIZE_BYTES> handshake_request_bytes;
        BoltError build_err = build_handshake_request(proposed_versions, handshake_request_bytes);
        if (build_err != BoltError::SUCCESS) {
            return build_err;
        }

        std::array<uint8_t, HANDSHAKE_RESPONSE_SIZE_BYTES> server_response_bytes;
        boost::system::error_code ec;

        // 使用 if constexpr 根据 StreamType 选择不同的 IO 操作
        if constexpr (std::is_base_of_v<std::basic_iostream<char>, StreamType> &&
                      // 进一步区分 basic_socket_iostream 和其他可能继承 std::iostream 的 Boost 类型
                      (std::is_same_v<StreamType, boost::asio::ip::tcp::iostream> || std::is_same_v<StreamType, boost::asio::basic_socket_iostream<boost::asio::ip::tcp>>)) {
            // 这是 boost::asio::ip::tcp::iostream 或其基类 basic_socket_iostream
            // 使用标准的 iostream 成员函数
            stream.write(reinterpret_cast<const char*>(handshake_request_bytes.data()), HANDSHAKE_REQUEST_SIZE_BYTES);
            if (stream.fail()) return BoltError::NETWORK_ERROR;
            stream.flush();
            if (stream.fail()) return BoltError::NETWORK_ERROR;

            stream.read(reinterpret_cast<char*>(server_response_bytes.data()), HANDSHAKE_RESPONSE_SIZE_BYTES);
            if (stream.fail() || static_cast<size_t>(stream.gcount()) != HANDSHAKE_RESPONSE_SIZE_BYTES) {
                return BoltError::NETWORK_ERROR;
            }
        } else {
            // 假设是其他 Boost.ASIO 同步流类型，如 ssl::stream 或 ip::tcp::socket
            // 使用 boost::asio::write 和 boost::asio::read 自由函数
            boost::asio::write(stream, boost::asio::buffer(handshake_request_bytes), ec);
            if (ec) {
                return BoltError::NETWORK_ERROR;
            }

            boost::asio::read(stream, boost::asio::buffer(server_response_bytes), ec);
            if (ec) {
                return BoltError::NETWORK_ERROR;
            }
        }

        return parse_handshake_response(server_response_bytes, out_negotiated_version);
    }

}  // namespace boltprotocol

#endif  // BOLTPROTOCOL_HANDSHAKE_H