#include <iostream>
#include <utility>  // For std::move

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

// For async placeholders
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace neo4j_bolt_transport {
    namespace internal {

        // --- Constructor, Destructor, Move Operations ---
        BoltPhysicalConnection::BoltPhysicalConnection(BoltConnectionConfig config, boost::asio::io_context& io_ctx, std::shared_ptr<spdlog::logger> logger_ptr)
            : id_(next_connection_id_counter_++), conn_config_(std::move(config)), io_context_ref_(io_ctx), logger_(std::move(logger_ptr)), current_state_(InternalState::FRESH), negotiated_bolt_version_(0, 0), creation_timestamp_(std::chrono::steady_clock::now()) {
            last_used_timestamp_.store(creation_timestamp_, std::memory_order_relaxed);
            if (logger_) {
                logger_->debug("[ConnLC {}] Constructed. Target: {}:{}", id_, conn_config_.target_host, conn_config_.target_port);
            }
        }

        BoltPhysicalConnection::~BoltPhysicalConnection() {
            if (logger_) {
                logger_->debug("[ConnLC {}] Destructing. Current state: {}", id_, _get_current_state_as_string());
            }
            if (current_state_.load(std::memory_order_relaxed) != InternalState::DEFUNCT) {
                terminate(false);
            }
        }

        BoltPhysicalConnection::BoltPhysicalConnection(BoltPhysicalConnection&& other) noexcept
            : id_(other.id_),
              conn_config_(std::move(other.conn_config_)),
              io_context_ref_(other.io_context_ref_),
              logger_(std::move(other.logger_)),
              // 使用新的同步成员变量名
              owned_socket_for_sync_plain_(std::move(other.owned_socket_for_sync_plain_)),
              plain_iostream_wrapper_(std::move(other.plain_iostream_wrapper_)),
              ssl_context_sync_(std::move(other.ssl_context_sync_)),
              ssl_stream_sync_(std::move(other.ssl_stream_sync_)),
              // chunked_writer_ 和 chunked_reader_ 好像在 .h 中没有被重命名，保持原样
              // 如果它们也应该区分同步/异步，也需要修改 .h 和这里
              // 假设它们是通用的，或者仅用于同步路径，检查 .h 文件确定
              // chunked_writer_(std::move(other.chunked_writer_)),
              // chunked_reader_(std::move(other.chunked_reader_)),
              negotiated_bolt_version_(other.negotiated_bolt_version_),
              server_agent_string_(std::move(other.server_agent_string_)),
              server_assigned_conn_id_(std::move(other.server_assigned_conn_id_)),
              utc_patch_active_(other.utc_patch_active_),
              creation_timestamp_(other.creation_timestamp_),
              last_error_code_(other.last_error_code_),
              last_error_message_(std::move(other.last_error_message_)) {
            current_state_.store(other.current_state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            last_used_timestamp_.store(other.last_used_timestamp_.load(std::memory_order_relaxed), std::memory_order_relaxed);

            other.id_ = static_cast<uint64_t>(-1);  // 标记 other 为无效
            other.current_state_.store(InternalState::DEFUNCT, std::memory_order_relaxed);
            if (logger_) {
                logger_->trace("[ConnLC {}] Move constructed from (now defunct) old connection.", id_);
            }
        }

        BoltPhysicalConnection& BoltPhysicalConnection::operator=(BoltPhysicalConnection&& other) noexcept {
            if (this != &other) {
                if (logger_) {
                    logger_->trace("[ConnLC {}] Move assigning from old ID {}. Current state before: {}", id_, other.id_, _get_current_state_as_string());
                }
                terminate(false);  // 关闭当前资源

                id_ = other.id_;
                conn_config_ = std::move(other.conn_config_);
                // io_context_ref_ 是引用，不能重新赋值
                logger_ = std::move(other.logger_);
                // 使用新的同步成员变量名
                owned_socket_for_sync_plain_ = std::move(other.owned_socket_for_sync_plain_);
                plain_iostream_wrapper_ = std::move(other.plain_iostream_wrapper_);
                ssl_context_sync_ = std::move(other.ssl_context_sync_);
                ssl_stream_sync_ = std::move(other.ssl_stream_sync_);
                // chunked_writer_ = std::move(other.chunked_writer_);
                // chunked_reader_ = std::move(other.chunked_reader_);
                current_state_.store(other.current_state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                negotiated_bolt_version_ = other.negotiated_bolt_version_;
                server_agent_string_ = std::move(other.server_agent_string_);
                server_assigned_conn_id_ = std::move(other.server_assigned_conn_id_);
                utc_patch_active_ = other.utc_patch_active_;
                creation_timestamp_ = other.creation_timestamp_;
                last_used_timestamp_.store(other.last_used_timestamp_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                last_error_code_ = other.last_error_code_;
                last_error_message_ = std::move(other.last_error_message_);

                other.id_ = static_cast<uint64_t>(-1);
                other.current_state_.store(InternalState::DEFUNCT, std::memory_order_relaxed);
                if (logger_) {
                    logger_->trace("[ConnLC {}] Move assignment complete.", id_);
                }
            }
            return *this;
        }

        void BoltPhysicalConnection::_reset_resources_and_state(bool called_from_destructor) {
            if (logger_) logger_->trace("[ConnLC {}] Resetting resources. From dtor: {}. Current state: {}", id_, called_from_destructor, _get_current_state_as_string());

            negotiated_bolt_version_ = boltprotocol::versions::Version(0, 0);
            server_agent_string_.clear();
            server_assigned_conn_id_.clear();
            utc_patch_active_ = false;

            // chunked_reader_.reset(); // 如果它们是成员
            // chunked_writer_.reset();

            // 重置同步资源
            if (ssl_stream_sync_) {
                boost::system::error_code ec_ssl_shutdown, ec_tcp_close, ec_tcp_shutdown;
                auto& lowest_socket = ssl_stream_sync_->lowest_layer();
                if (lowest_socket.is_open()) {
                    if (!called_from_destructor && !(SSL_get_shutdown(ssl_stream_sync_->native_handle()) & SSL_RECEIVED_SHUTDOWN)) {
                        try {
                            ssl_stream_sync_->shutdown(ec_ssl_shutdown);
                        } catch (...) { /*ignore*/
                        }
                    }
                    // 在某些情况下，shutdown_both 可能会在 close 之前导致问题，特别是如果对方没有正确关闭。
                    // 通常 close() 就足够了，它会隐式地做必要的清理。
                    try {
                        lowest_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec_tcp_shutdown);
                    } catch (...) { /*ignore*/
                    }
                    try {
                        lowest_socket.close(ec_tcp_close);
                    } catch (...) { /*ignore*/
                    }
                }
                ssl_stream_sync_.reset();
            }
            if (ssl_context_sync_) ssl_context_sync_.reset();

            if (plain_iostream_wrapper_) {
                // iostream 的 socket 由其内部管理，当 iostream 被销毁时，socket 也会被关闭。
                // 我们需要确保 plain_iostream_wrapper_ 包装的 socket (即 owned_socket_for_sync_plain_) 已经被转移所有权或不再有效。
                // 在 _stage_tcp_connect 中，如果创建了 plain_iostream_wrapper_，则 owned_socket_for_sync_plain_ 会被 reset。
                if (!called_from_destructor && plain_iostream_wrapper_->good()) {
                    try {
                        plain_iostream_wrapper_->flush();
                    } catch (...) { /*ignore*/
                    }
                }
                plain_iostream_wrapper_.reset();  // 这会析构 iostream，从而关闭其内部的 socket
            }
            // owned_socket_for_sync_plain_ 应该只在它没有被转移给 iostream 或 ssl_stream 时才由这里直接关闭。
            // 在当前的同步逻辑中：
            // - 如果是明文连接，它被移动给 plain_iostream_wrapper_。
            // - 如果是SSL连接，它被移动给 ssl_stream_sync_ 的构造函数。
            // 所以，理论上，当 _reset_resources_and_state 被调用时，owned_socket_for_sync_plain_ 应该是 nullptr。
            // 但为了保险，如果它仍然存在且打开，则关闭它。
            if (owned_socket_for_sync_plain_ && owned_socket_for_sync_plain_->is_open()) {
                if (logger_) logger_->warn("[ConnLC {}] owned_socket_for_sync_plain_ 在重置期间意外打开，正在关闭。", id_);
                boost::system::error_code ec;
                try {
                    owned_socket_for_sync_plain_->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                } catch (...) { /*ignore*/
                }
                try {
                    owned_socket_for_sync_plain_->close(ec);
                } catch (...) { /*ignore*/
                }
            }
            owned_socket_for_sync_plain_.reset();

            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            current_state_.store(InternalState::FRESH, std::memory_order_relaxed);
            last_used_timestamp_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
        }

        boltprotocol::BoltError BoltPhysicalConnection::establish() {
            InternalState expected_fresh = InternalState::FRESH;
            if (!current_state_.compare_exchange_strong(expected_fresh, InternalState::TCP_CONNECTING, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                if (current_state_.load(std::memory_order_relaxed) == InternalState::READY) {
                    if (logger_) logger_->debug("[ConnLC {}] Establish called but already READY.", id_);
                    return boltprotocol::BoltError::SUCCESS;
                }
                std::string msg = "Establish called in invalid state " + _get_current_state_as_string();
                if (logger_) logger_->warn("[ConnLC {}] {}", id_, msg);
                if (current_state_.load(std::memory_order_relaxed) != InternalState::READY && current_state_.load(std::memory_order_relaxed) != InternalState::FAILED_SERVER_REPORTED) {
                    _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                }
                return last_error_code_ != boltprotocol::BoltError::SUCCESS ? last_error_code_ : boltprotocol::BoltError::UNKNOWN_ERROR;
            }
            if (logger_) logger_->info("[ConnLC {}] Establishing connection to {}:{}", id_, conn_config_.target_host, conn_config_.target_port);

            // 在开始新的连接尝试之前，确保旧资源已清理
            _reset_resources_and_state(false);
            // 重置后状态应为 FRESH，但我们已经将其CAS为 TCP_CONNECTING，所以直接继续
            current_state_.store(InternalState::TCP_CONNECTING, std::memory_order_relaxed);

            boltprotocol::BoltError err = _stage_tcp_connect();
            if (err != boltprotocol::BoltError::SUCCESS) {
                // _stage_tcp_connect 内部会调用 _mark_as_defunct 和 _reset_resources_and_state (如果需要的话)
                // 这里确保如果它没有重置，我们在这里重置
                if (current_state_.load(std::memory_order_relaxed) != InternalState::FRESH && current_state_.load(std::memory_order_relaxed) != InternalState::DEFUNCT) {
                    _reset_resources_and_state(false);
                }
                return last_error_code_;
            }

            if (conn_config_.encryption_enabled) {
                err = _stage_ssl_context_setup();
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _reset_resources_and_state(false);
                    return last_error_code_;
                }
                err = _stage_ssl_handshake();
                if (err != boltprotocol::BoltError::SUCCESS) {
                    _reset_resources_and_state(false);
                    return last_error_code_;
                }
            }

            err = _stage_bolt_handshake();
            if (err != boltprotocol::BoltError::SUCCESS) {
                _reset_resources_and_state(false);
                return last_error_code_;
            }

            err = _stage_send_hello_and_initial_auth();
            if (err != boltprotocol::BoltError::SUCCESS) {
                _reset_resources_and_state(false);
                return last_error_code_;
            }

            if (current_state_.load(std::memory_order_relaxed) != InternalState::READY) {
                std::string msg = "Connection did not reach READY state after establish sequence. Final state: " + _get_current_state_as_string();
                if (logger_) logger_->error("[ConnLC {}] {}", id_, msg);
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                _reset_resources_and_state(false);
                return last_error_code_;
            }

            mark_as_used();
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->info("[ConnLC {}] Connection established and ready. Bolt version: {}.{}. Server: {}", id_, (int)negotiated_bolt_version_.major, (int)negotiated_bolt_version_.minor, server_agent_string_);
            return boltprotocol::BoltError::SUCCESS;
        }

        // terminate, mark_as_used, is_encrypted, is_ready_for_queries, is_defunct 保持不变
        // ...
        boltprotocol::BoltError BoltPhysicalConnection::terminate(bool send_goodbye) {
            InternalState current_s = current_state_.load(std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnLC {}] Terminating. Current state: {}. Send goodbye: {}", id_, _get_current_state_as_string(), send_goodbye);

            // 如果已经是 FRESH 并且没有任何流或套接字资源，则无需操作
            if (current_s == InternalState::FRESH && !plain_iostream_wrapper_ && !ssl_stream_sync_ && !owned_socket_for_sync_plain_) {
                if (logger_) logger_->trace("[ConnLC {}] Terminate called on already clean/fresh connection.", id_);
                current_state_.store(InternalState::DEFUNCT, std::memory_order_relaxed);  // 确保标记为终止
                return boltprotocol::BoltError::SUCCESS;
            }

            // 原子地将状态设置为 DEFUNCT，以防止其他操作干扰关闭过程
            InternalState expected_s_for_cas = current_s;
            while (expected_s_for_cas != InternalState::DEFUNCT && !current_state_.compare_exchange_weak(expected_s_for_cas, InternalState::DEFUNCT, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                // 在 compare_exchange_weak 失败时，expected_s_for_cas 会被更新为当前值
                // 这意味着另一个线程可能已经改变了状态，循环会重试或在 expected_s_for_cas 变为 DEFUNCT 时退出
            }
            // 此时，current_state_ 要么已经是 DEFUNCT，要么刚刚被这个线程设置为 DEFUNCT

            if (send_goodbye && current_s >= InternalState::BOLT_HANDSHAKEN &&          // 至少完成了 Bolt 握手
                current_s != InternalState::DEFUNCT &&                                  // 之前不是 DEFUNCT
                !(negotiated_bolt_version_ < boltprotocol::versions::Version(3, 0))) {  // Bolt 3.0+ 支持 GOODBYE

                bool can_send_goodbye = false;
                if (conn_config_.encryption_enabled) {
                    if (ssl_stream_sync_ && ssl_stream_sync_->lowest_layer().is_open()) can_send_goodbye = true;
                } else {
                    if (plain_iostream_wrapper_ && plain_iostream_wrapper_->good()) can_send_goodbye = true;
                }

                if (can_send_goodbye) {
                    if (logger_) logger_->trace("[ConnLC {}] Attempting to send GOODBYE.", id_);
                    std::vector<uint8_t> goodbye_payload;
                    boltprotocol::PackStreamWriter ps_writer(goodbye_payload);  // 假设 PackStreamWriter 可以用于空缓冲区
                    if (boltprotocol::serialize_goodbye_message(ps_writer) == boltprotocol::BoltError::SUCCESS) {
                        // _send_chunked_payload 内部会处理错误并可能调用 _mark_as_defunct
                        // 但由于我们已经将状态设为 DEFUNCT，所以它不会再次调用 _mark_as_defunct
                        // 注意：这里的 _send_chunked_payload 是同步版本
                        boltprotocol::BoltError goodbye_err = _send_chunked_payload(goodbye_payload);
                        if (goodbye_err != boltprotocol::BoltError::SUCCESS && logger_) {
                            logger_->warn("[ConnLC {}] Sending GOODBYE failed: {}", id_, error::bolt_error_to_string(goodbye_err));
                        } else if (logger_) {
                            logger_->trace("[ConnLC {}] GOODBYE message sent.", id_);
                        }
                    }
                } else {
                    if (logger_) logger_->trace("[ConnLC {}] Cannot send GOODBYE (stream not ready or Bolt version too low). Current state was: {}", id_, (int)current_s);
                }
            }
            _reset_resources_and_state(false);  // 实际清理资源
            return boltprotocol::BoltError::SUCCESS;
        }

        void BoltPhysicalConnection::mark_as_used() {
            last_used_timestamp_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
        }

        bool BoltPhysicalConnection::is_encrypted() const {
            return conn_config_.encryption_enabled && (ssl_stream_sync_ != nullptr) && (current_state_.load(std::memory_order_relaxed) >= InternalState::SSL_HANDSHAKEN);
        }

        // --- Asynchronous Lifecycle Methods (Placeholders) ---
        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::establish_async() {
            // 类似同步的 establish，但所有阶段都调用 *_async 版本
            // co_await _stage_tcp_connect_async();
            // ...
            if (logger_) logger_->debug("[ConnLCAsync {}] establish_async called.", id_);
            // 这部分需要大量重写为协程风格
            co_return boltprotocol::BoltError::UNKNOWN_ERROR;  // Placeholder
        }

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::terminate_async(bool send_goodbye) {
            if (logger_) logger_->debug("[ConnLCAsync {}] terminate_async called. Send goodbye: {}", id_, send_goodbye);
            // 类似同步的 terminate，但 _send_chunked_payload_async (如果发送 GOODBYE)
            // 资源清理 _reset_resources_and_state 仍然是同步的，因为它不涉及I/O
            co_return boltprotocol::BoltError::UNKNOWN_ERROR;  // Placeholder
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport