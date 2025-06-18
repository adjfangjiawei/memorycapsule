#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_PHYSICAL_CONNECTION_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_PHYSICAL_CONNECTION_H

#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bolt_connection_config.h"
#include "boltprotocol/chunking.h"
#include "boltprotocol/handshake.h"
#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "spdlog/spdlog.h"

namespace neo4j_bolt_transport {

    class SessionHandle;
    class AsyncSessionHandle;

    namespace internal {

        class BoltPhysicalConnection {
          public:
            using PooledConnection = std::unique_ptr<BoltPhysicalConnection>;
            enum class InternalState { FRESH, TCP_CONNECTING, TCP_CONNECTED, SSL_CONTEXT_SETUP, SSL_HANDSHAKING, SSL_HANDSHAKEN, BOLT_HANDSHAKING, BOLT_HANDSHAKEN, HELLO_AUTH_SENT, READY, STREAMING, AWAITING_SUMMARY, FAILED_SERVER_REPORTED, DEFUNCT };

            BoltPhysicalConnection(BoltConnectionConfig config, boost::asio::io_context& io_ctx, std::shared_ptr<spdlog::logger> logger_ptr);
            ~BoltPhysicalConnection();

            BoltPhysicalConnection(const BoltPhysicalConnection&) = delete;
            BoltPhysicalConnection& operator=(const BoltPhysicalConnection&) = delete;
            BoltPhysicalConnection(BoltPhysicalConnection&& other) noexcept;
            BoltPhysicalConnection& operator=(BoltPhysicalConnection&& other) noexcept;

            // --- 同步 API ---
            boltprotocol::BoltError establish();
            boltprotocol::BoltError terminate(bool send_goodbye = true);
            boltprotocol::BoltError ping(std::chrono::milliseconds timeout);
            bool is_ready_for_queries() const;
            bool is_defunct() const;
            boltprotocol::BoltError get_last_error_code() const {
                return last_error_code_;
            }
            std::string get_last_error_message() const {
                return last_error_message_;
            }
            uint64_t get_id() const {
                return id_;
            }
            const boltprotocol::versions::Version& get_bolt_version() const {
                return negotiated_bolt_version_;
            }
            bool is_utc_patch_active() const {
                return utc_patch_active_;
            }
            const std::string& get_server_agent() const {
                return server_agent_string_;
            }
            const std::string& get_connection_id() const {
                return server_assigned_conn_id_;
            }
            const BoltConnectionConfig& get_config() const {
                return conn_config_;
            }
            std::shared_ptr<spdlog::logger> get_logger() const {
                return logger_;
            }
            boost::asio::io_context& get_io_context() {
                return io_context_ref_;
            }
            std::chrono::steady_clock::time_point get_creation_timestamp() const {
                return creation_timestamp_;
            }
            std::chrono::steady_clock::time_point get_last_used_timestamp() const {
                return last_used_timestamp_.load(std::memory_order_relaxed);
            }
            void mark_as_used();
            bool is_encrypted() const;

            using MessageHandler = std::function<boltprotocol::BoltError(boltprotocol::MessageTag tag, const std::vector<uint8_t>& payload, BoltPhysicalConnection& connection)>;
            boltprotocol::BoltError send_request_receive_stream(const std::vector<uint8_t>& request_payload, MessageHandler record_handler, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure);
            boltprotocol::BoltError send_request_receive_summary(const std::vector<uint8_t>& request_payload, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure);
            boltprotocol::BoltError perform_reset();
            boltprotocol::BoltError perform_logon(const boltprotocol::LogonMessageParams& logon_params, boltprotocol::SuccessMessageParams& out_success);
            boltprotocol::BoltError perform_logoff(boltprotocol::SuccessMessageParams& out_success);

            // --- 异步 API ---
            // Corrected declaration based on previous placeholder implementation
            boost::asio::awaitable<boltprotocol::BoltError> establish_async();  // <--- 修改：确保返回类型和参数与实现匹配

            boost::asio::awaitable<boltprotocol::BoltError> terminate_async(bool send_goodbye = true);
            boost::asio::awaitable<boltprotocol::BoltError> ping_async(std::chrono::milliseconds timeout);

            using AsyncMessageHandler = std::function<boost::asio::awaitable<std::pair<boltprotocol::BoltError, bool>>(boltprotocol::MessageTag tag, std::vector<uint8_t> payload, BoltPhysicalConnection& connection)>;
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> send_request_receive_stream_async(std::vector<uint8_t> request_payload, AsyncMessageHandler record_handler);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> send_request_receive_summary_async(std::vector<uint8_t> request_payload);
            boost::asio::awaitable<boltprotocol::BoltError> perform_reset_async();
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> perform_logon_async(boltprotocol::LogonMessageParams logon_params);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> perform_logoff_async();

          private:
            friend class neo4j_bolt_transport::SessionHandle;
            friend class neo4j_bolt_transport::AsyncSessionHandle;

            // --- 同步连接阶段 ---
            boltprotocol::BoltError _stage_tcp_connect();
            boltprotocol::BoltError _stage_ssl_context_setup();
            boltprotocol::BoltError _stage_ssl_handshake();
            boltprotocol::BoltError _stage_bolt_handshake();
            boltprotocol::BoltError _stage_send_hello_and_initial_auth();

            // --- 异步连接阶段 ---
            // Pass socket by reference for async operations that create it temporarily within the coroutine
            boost::asio::awaitable<boltprotocol::BoltError> _stage_tcp_connect_async(boost::asio::ip::tcp::socket& socket, std::chrono::milliseconds timeout);
            boost::asio::awaitable<boltprotocol::BoltError> _stage_ssl_handshake_async(boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>& stream, std::chrono::milliseconds timeout);

            // These need to be adapted to use the socket/stream established by the above _async methods.
            // They won't take raw socket/stream pointers if establish_async manages the stream internally.
            boost::asio::awaitable<boltprotocol::BoltError> _stage_bolt_handshake_async(/* StreamType& stream, */ std::chrono::milliseconds timeout);  // Needs active stream
            boost::asio::awaitable<boltprotocol::BoltError> _stage_send_hello_and_initial_auth_async(/* StreamType& stream */);                        // Needs active stream

            void _prepare_logon_params_from_config(boltprotocol::LogonMessageParams& out_params) const;
            boltprotocol::BoltError _execute_logon_message(const boltprotocol::LogonMessageParams& params, boltprotocol::SuccessMessageParams& out_success, boltprotocol::FailureMessageParams& out_failure);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> _execute_logon_message_async(boltprotocol::LogonMessageParams params,
                                                                                                                                        boost::asio::ip::tcp::socket* plain_socket_unused,                                 // Parameter kept for signature consistency for now
                                                                                                                                        boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>* ssl_socket_stream_unused  // Parameter kept
            );

            // 低级IO (同步)
            boltprotocol::BoltError _write_to_active_stream(const uint8_t* data, size_t size);
            boltprotocol::BoltError _read_from_active_stream(uint8_t* buffer, size_t size_to_read, size_t& bytes_read);
            // 低级IO (异步) - These need to operate on the currently active async stream
            boost::asio::awaitable<boltprotocol::BoltError> _write_to_active_stream_async(const std::vector<uint8_t>& data);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> _read_from_active_stream_async(size_t size_to_read);

            // Chunking (同步)
            boltprotocol::BoltError _send_chunked_payload(const std::vector<uint8_t>& payload);
            boltprotocol::BoltError _receive_chunked_payload(std::vector<uint8_t>& out_payload);
            // Chunking (异步)
            boost::asio::awaitable<boltprotocol::BoltError> _send_chunked_payload_async(std::vector<uint8_t> payload);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> _receive_chunked_payload_async();

            boltprotocol::BoltError _peek_message_tag(const std::vector<uint8_t>& payload, boltprotocol::MessageTag& out_tag) const;

            void _reset_resources_and_state(bool called_from_destructor = false);
            void _update_metadata_from_hello_success(const boltprotocol::SuccessMessageParams& meta);
            void _update_metadata_from_logon_success(const boltprotocol::SuccessMessageParams& meta);
            boltprotocol::BoltError _classify_and_set_server_failure(const boltprotocol::FailureMessageParams& meta);
            void _mark_as_defunct(boltprotocol::BoltError reason, const std::string& message = "");
            std::string _get_current_state_as_string() const;

            // template <typename SyncOperation>
            // boost::asio::awaitable<boltprotocol::BoltError> _run_sync_op_with_timeout(SyncOperation op, std::chrono::milliseconds timeout);

            uint64_t id_;
            BoltConnectionConfig conn_config_;
            boost::asio::io_context& io_context_ref_;
            std::shared_ptr<spdlog::logger> logger_;

            // 同步操作使用的资源
            std::unique_ptr<boost::asio::ip::tcp::socket> owned_socket_for_sync_plain_;
            std::unique_ptr<boost::asio::ip::tcp::iostream> plain_iostream_wrapper_;  // Wraps owned_socket_for_sync_plain_ if not SSL
            std::unique_ptr<boost::asio::ssl::context> ssl_context_sync_;
            std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ssl_stream_sync_;  // Wraps owned_socket_for_sync_plain_ if SSL

            // For async operations, the stream/socket is typically managed within the coroutine or passed around.
            // If establish_async creates and returns a stream, or stores it in a member (needs care for concurrency).
            // For now, assuming async methods will manage their own stream or one is passed.
            // Example: std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> async_ssl_stream_;
            // Example: std::unique_ptr<boost::asio::ip::tcp::socket> async_plain_socket_;

            std::atomic<InternalState> current_state_;
            boltprotocol::versions::Version negotiated_bolt_version_;
            std::string server_agent_string_;
            std::string server_assigned_conn_id_;
            bool utc_patch_active_ = false;

            std::chrono::steady_clock::time_point creation_timestamp_;
            std::atomic<std::chrono::steady_clock::time_point> last_used_timestamp_;

            boltprotocol::BoltError last_error_code_ = boltprotocol::BoltError::SUCCESS;
            std::string last_error_message_;

            static std::atomic<uint64_t> next_connection_id_counter_;
        };

    }  // namespace internal
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_PHYSICAL_CONNECTION_H