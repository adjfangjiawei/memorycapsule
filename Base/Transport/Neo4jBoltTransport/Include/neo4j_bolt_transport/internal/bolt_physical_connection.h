#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_PHYSICAL_CONNECTION_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_PHYSICAL_CONNECTION_H

#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>  // <--- NEW for async
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
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
    class AsyncSessionHandle;  // Forward declare

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

            // --- Synchronous API ---
            boltprotocol::BoltError establish();
            boltprotocol::BoltError terminate(bool send_goodbye = true);
            boltprotocol::BoltError ping(std::chrono::milliseconds timeout);  // Timeout currently not strictly enforced in sync ping

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
            }  // <--- NEW
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
            std::chrono::steady_clock::time_point get_creation_timestamp() const {
                return creation_timestamp_;
            }
            std::chrono::steady_clock::time_point get_last_used_timestamp() const {
                return last_used_timestamp_.load(std::memory_order_relaxed);
            }
            void mark_as_used();
            bool is_encrypted() const;

            // Synchronous message handler
            using MessageHandler = std::function<boltprotocol::BoltError(boltprotocol::MessageTag tag, const std::vector<uint8_t>& payload, BoltPhysicalConnection& connection)>;

            boltprotocol::BoltError send_request_receive_stream(const std::vector<uint8_t>& request_payload, MessageHandler record_handler, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure);
            boltprotocol::BoltError send_request_receive_summary(const std::vector<uint8_t>& request_payload, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure);
            boltprotocol::BoltError perform_reset();
            boltprotocol::BoltError perform_logon(const boltprotocol::LogonMessageParams& logon_params, boltprotocol::SuccessMessageParams& out_success);
            boltprotocol::BoltError perform_logoff(boltprotocol::SuccessMessageParams& out_success);

            // --- Asynchronous API (using boost::asio::awaitable) ---
            boost::asio::awaitable<boltprotocol::BoltError> establish_async();
            boost::asio::awaitable<boltprotocol::BoltError> terminate_async(bool send_goodbye = true);
            // ping_async might need a specific timeout parameter for async timer
            boost::asio::awaitable<boltprotocol::BoltError> ping_async(std::chrono::milliseconds timeout);

            // Asynchronous message handler
            // Returns: {error_code, should_continue_streaming}
            using AsyncMessageHandler = std::function<boost::asio::awaitable<std::pair<boltprotocol::BoltError, bool>>(boltprotocol::MessageTag tag, std::vector<uint8_t> payload, BoltPhysicalConnection& connection)>;

            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> send_request_receive_stream_async(std::vector<uint8_t> request_payload, AsyncMessageHandler record_handler);  // Failure is embedded in BoltError or thrown by awaitable

            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>>  // Success or Failure embedded in returned pair (or specific error struct)
            send_request_receive_summary_async(std::vector<uint8_t> request_payload);

            boost::asio::awaitable<boltprotocol::BoltError> perform_reset_async();
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> perform_logon_async(boltprotocol::LogonMessageParams logon_params);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> perform_logoff_async();

          private:
            friend class neo4j_bolt_transport::SessionHandle;
            friend class neo4j_bolt_transport::AsyncSessionHandle;  // For async

            // --- Synchronous connection stages ---
            boltprotocol::BoltError _stage_tcp_connect();
            boltprotocol::BoltError _stage_ssl_context_setup();
            boltprotocol::BoltError _stage_ssl_handshake();
            boltprotocol::BoltError _stage_bolt_handshake();
            boltprotocol::BoltError _stage_send_hello_and_initial_auth();

            // --- Asynchronous connection stages ---
            boost::asio::awaitable<boltprotocol::BoltError> _stage_tcp_connect_async();
            boost::asio::awaitable<boltprotocol::BoltError> _stage_ssl_context_setup_async();  // Might be mostly sync if context setup is not I/O bound
            boost::asio::awaitable<boltprotocol::BoltError> _stage_ssl_handshake_async();
            boost::asio::awaitable<boltprotocol::BoltError> _stage_bolt_handshake_async();
            boost::asio::awaitable<boltprotocol::BoltError> _stage_send_hello_and_initial_auth_async();

            // Auth helper (sync, async version might call this or be separate)
            void _prepare_logon_params_from_config(boltprotocol::LogonMessageParams& out_params) const;
            boltprotocol::BoltError _execute_logon_message(const boltprotocol::LogonMessageParams& params, boltprotocol::SuccessMessageParams& out_success, boltprotocol::FailureMessageParams& out_failure);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> _execute_logon_message_async(boltprotocol::LogonMessageParams params);

            // Low-level IO (sync)
            boltprotocol::BoltError _write_to_active_stream(const uint8_t* data, size_t size);
            boltprotocol::BoltError _read_from_active_stream(uint8_t* buffer, size_t size_to_read, size_t& bytes_read);
            // Low-level IO (async)
            boost::asio::awaitable<boltprotocol::BoltError> _write_to_active_stream_async(std::vector<uint8_t> data);  // Takes ownership
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> _read_from_active_stream_async(size_t size_to_read);

            // Chunking logic (sync)
            boltprotocol::BoltError _send_chunked_payload(const std::vector<uint8_t>& payload);
            boltprotocol::BoltError _receive_chunked_payload(std::vector<uint8_t>& out_payload);
            // Chunking logic (async)
            boost::asio::awaitable<boltprotocol::BoltError> _send_chunked_payload_async(std::vector<uint8_t> payload);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> _receive_chunked_payload_async();

            boltprotocol::BoltError _peek_message_tag(const std::vector<uint8_t>& payload, boltprotocol::MessageTag& out_tag) const;

            // State and metadata
            void _reset_resources_and_state(bool called_from_destructor = false);
            void _update_metadata_from_hello_success(const boltprotocol::SuccessMessageParams& meta);
            void _update_metadata_from_logon_success(const boltprotocol::SuccessMessageParams& meta);
            boltprotocol::BoltError _classify_and_set_server_failure(const boltprotocol::FailureMessageParams& meta);
            void _mark_as_defunct(boltprotocol::BoltError reason, const std::string& message = "");
            std::string _get_current_state_as_string() const;

            uint64_t id_;
            BoltConnectionConfig conn_config_;
            boost::asio::io_context& io_context_ref_;  // All async operations run on this
            std::shared_ptr<spdlog::logger> logger_;

            // For synchronous operations, one of these is active
            std::unique_ptr<boost::asio::ip::tcp::socket> owned_socket_for_sync_plain_;  // If sync + plain
            std::unique_ptr<boost::asio::ip::tcp::iostream> plain_iostream_wrapper_;     // Wraps owned_socket_for_sync_plain_
            // For SSL, ssl_stream_sync_ takes ownership of a socket
            std::unique_ptr<boost::asio::ssl::context> ssl_context_sync_;  // Context for sync SSL
            std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ssl_stream_sync_;

            // For asynchronous operations, one of these is active
            // These will be constructed when an async operation starts on a connection
            // boost::asio::ip::tcp::socket async_socket_; // Re-created for each async establish
            // boost::asio::ssl::context async_ssl_context_; // Re-created or reused
            // std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>> async_ssl_stream_; // Wraps async_socket_

            // Simpler: Re-use sync socket/stream objects for async if possible, or manage active stream pointer
            // Let's assume for now that a connection is either used synchronously OR asynchronously for its lifetime
            // to simplify stream management. Or, the async methods will construct their own socket/stream objects.
            // For this iteration, async methods will re-use/manage their own socket based on conn_config_

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