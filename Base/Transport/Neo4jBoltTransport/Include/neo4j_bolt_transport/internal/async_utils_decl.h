// Include/neo4j_bolt_transport/internal/async_utils_decl.h
#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_UTILS_DECL_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_ASYNC_UTILS_DECL_H

#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>  // For buffer concepts
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <chrono>
#include <string>
#include <utility>

#include "boltprotocol/bolt_errors_versions.h"
#include "neo4j_bolt_transport/internal/i_async_context_callbacks.h"

namespace neo4j_bolt_transport {
    namespace internal {
        namespace async_utils {

            // Timeout wrapper for read operations
            template <typename Stream, typename MutableBufferSequence>
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::size_t>> async_read_with_timeout(IAsyncContextCallbacks* callbacks,
                                                                                                            Stream& stream,
                                                                                                            MutableBufferSequence buffers,  // Must be mutable
                                                                                                            std::chrono::milliseconds timeout_duration,
                                                                                                            const std::string& operation_name);

            // Timeout wrapper for write operations
            template <typename Stream, typename ConstBufferSequence>
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::size_t>> async_write_with_timeout(IAsyncContextCallbacks* callbacks,
                                                                                                             Stream& stream,
                                                                                                             ConstBufferSequence buffers,  // Can be const
                                                                                                             std::chrono::milliseconds timeout_duration,
                                                                                                             const std::string& operation_name);

        }  // namespace async_utils
    }  // namespace internal
}  // namespace neo4j_bolt_transport

#include "async_utils_impl.h"

#endif