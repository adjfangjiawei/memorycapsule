#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_PHYSICAL_CONNECTION_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_PHYSICAL_CONNECTION_H

#include <atomic>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/basic_socket_iostream.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "bolt_connection_config.h"
#include "boltprotocol/handshake.h"
#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "neo4j_bolt_transport/internal/async_types.h"
#include "neo4j_bolt_transport/internal/i_async_context_callbacks.h"
#include "neo4j_bolt_transport/result_summary.h"
#include "spdlog/spdlog.h"

namespace neo4j_bolt_transport {

    class SessionHandle;
    class AsyncSessionHandle;
    class AsyncResultStream;

    namespace internal {

        class BoltPhysicalConnection : public std::enable_shared_from_this<BoltPhysicalConnection>, public IAsyncContextCallbacks {
          public:
            friend class neo4j_bolt_transport::AsyncResultStream;

            using PooledConnection = std::unique_ptr<BoltPhysicalConnection>;
            enum class InternalState {
                FRESH,
                ASYNC_TCP_CONNECTING,
                TCP_CONNECTING,
                TCP_CONNECTED,
                SSL_CONTEXT_SETUP,
                ASYNC_SSL_HANDSHAKING,
                SSL_HANDSHAKING,
                SSL_HANDSHAKEN,
                ASYNC_BOLT_HANDSHAKING,
                BOLT_HANDSHAKING,
                BOLT_HANDSHAKEN,
                ASYNC_BOLT_HANDSHAKEN,
                ASYNC_HELLO_AUTH_SENT,
                HELLO_AUTH_SENT,
                ASYNC_READY,
                READY,
                ASYNC_STREAMING,
                STREAMING,
                ASYNC_AWAITING_SUMMARY,
                AWAITING_SUMMARY,
                FAILED_SERVER_REPORTED,
                DEFUNCT
            };

            BoltPhysicalConnection(BoltConnectionConfig config, boost::asio::io_context& io_ctx, std::shared_ptr<spdlog::logger> logger_ptr);
            ~BoltPhysicalConnection() override;

            BoltPhysicalConnection(const BoltPhysicalConnection&) = delete;
            BoltPhysicalConnection& operator=(const BoltPhysicalConnection&) = delete;
            BoltPhysicalConnection(BoltPhysicalConnection&& other) noexcept;
            BoltPhysicalConnection& operator=(BoltPhysicalConnection&& other) noexcept;

            // --- Synchronous API ---
            boltprotocol::BoltError establish();
            boltprotocol::BoltError terminate(bool send_goodbye = true);
            boltprotocol::BoltError ping(std::chrono::milliseconds timeout);
            bool is_ready_for_queries() const;
            bool is_defunct() const;
            boltprotocol::BoltError get_last_error_code() const;
            std::string get_last_error_message() const;
            uint64_t get_id() const;
            const boltprotocol::versions::Version& get_bolt_version() const;
            bool is_utc_patch_active() const;
            const std::string& get_server_agent() const;
            const std::string& get_connection_id() const;
            const BoltConnectionConfig& get_config() const;
            boost::asio::io_context& get_io_context();
            std::chrono::steady_clock::time_point get_creation_timestamp() const;
            std::chrono::steady_clock::time_point get_last_used_timestamp() const;
            void mark_as_used();
            bool is_encrypted() const;

            using MessageHandler = std::function<boltprotocol::BoltError(boltprotocol::MessageTag tag, const std::vector<uint8_t>& payload, BoltPhysicalConnection& connection)>;
            boltprotocol::BoltError send_request_receive_stream(const std::vector<uint8_t>& request_payload, MessageHandler record_handler, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure);
            boltprotocol::BoltError send_request_receive_summary(const std::vector<uint8_t>& request_payload, boltprotocol::SuccessMessageParams& out_summary, boltprotocol::FailureMessageParams& out_failure);
            boltprotocol::BoltError perform_reset();
            boltprotocol::BoltError perform_logon(const boltprotocol::LogonMessageParams& logon_params, boltprotocol::SuccessMessageParams& out_success);
            boltprotocol::BoltError perform_logoff(boltprotocol::SuccessMessageParams& out_success);

            // --- Asynchronous API ---
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, ActiveAsyncStreamContext>> establish_async();
            boost::asio::awaitable<boltprotocol::BoltError> terminate_async(bool send_goodbye = true);
            boost::asio::awaitable<boltprotocol::BoltError> ping_async(std::chrono::milliseconds timeout);

            // --- Static-like Async Messaging Utilities ---
            static boost::asio::awaitable<std::pair<boltprotocol::BoltError, ResultSummary>> send_request_receive_summary_async_static(
                internal::ActiveAsyncStreamContext& stream_ctx, const std::vector<uint8_t>& request_payload, const BoltConnectionConfig& conn_config_ref, std::shared_ptr<spdlog::logger> logger_ref, std::function<void(boltprotocol::BoltError, const std::string&)> error_handler);

            static boost::asio::awaitable<boltprotocol::BoltError> send_goodbye_async_static(internal::ActiveAsyncStreamContext& stream_ctx,
                                                                                             const BoltConnectionConfig& conn_config_ref,
                                                                                             std::shared_ptr<spdlog::logger> logger_ref,
                                                                                             std::function<void(boltprotocol::BoltError, const std::string&)> error_handler);

            std::shared_ptr<spdlog::logger> get_logger() const override;
            uint64_t get_id_for_logging() const override;
            void mark_as_defunct_from_async(boltprotocol::BoltError reason, const std::string& message) override;
            boltprotocol::BoltError get_last_error_code_from_async() const override;
            void _mark_as_defunct_internal(boltprotocol::BoltError reason, const std::string& message = "");

          private:
            friend class neo4j_bolt_transport::SessionHandle;
            friend class neo4j_bolt_transport::AsyncSessionHandle;

            // --- Static Private Asynchronous Chunking Helpers ---
            static boost::asio::awaitable<boltprotocol::BoltError> _send_chunked_payload_async_static_helper(
                internal::ActiveAsyncStreamContext& stream_ctx, std::vector<uint8_t> payload, const BoltConnectionConfig& conn_config_ref, std::shared_ptr<spdlog::logger> logger_ref, std::function<void(boltprotocol::BoltError, const std::string&)> error_handler);

            static boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> _receive_chunked_payload_async_static_helper(internal::ActiveAsyncStreamContext& stream_ctx,
                                                                                                                                                 const BoltConnectionConfig& conn_config_ref,
                                                                                                                                                 std::shared_ptr<spdlog::logger> logger_ref,
                                                                                                                                                 std::function<void(boltprotocol::BoltError, const std::string&)> error_handler);

            // --- Synchronous Lifecycle Stages ---
            boltprotocol::BoltError _stage_tcp_connect();
            boltprotocol::BoltError _stage_ssl_context_setup();
            boltprotocol::BoltError _stage_ssl_handshake();
            boltprotocol::BoltError _stage_bolt_handshake();
            boltprotocol::BoltError _stage_send_hello_and_initial_auth();

            // --- Asynchronous Lifecycle Stages (called by establish_async) ---
            boost::asio::awaitable<boltprotocol::BoltError> _stage_tcp_connect_async(boost::asio::ip::tcp::socket& socket, std::chrono::milliseconds timeout);
            boost::asio::awaitable<boltprotocol::BoltError> _stage_ssl_handshake_async(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& stream, std::chrono::milliseconds timeout);
            boost::asio::awaitable<boltprotocol::BoltError> _stage_bolt_handshake_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref, std::chrono::milliseconds timeout);
            boost::asio::awaitable<boltprotocol::BoltError> _stage_send_hello_and_initial_auth_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref);

            // --- Authentication Helpers ---
            void _prepare_logon_params_from_config(boltprotocol::LogonMessageParams& out_params) const;
            boltprotocol::BoltError _execute_logon_message(const boltprotocol::LogonMessageParams& params, boltprotocol::SuccessMessageParams& out_success, boltprotocol::FailureMessageParams& out_failure);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> _execute_logon_message_async(boltprotocol::LogonMessageParams params,
                                                                                                                                        std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> perform_logon_async(boltprotocol::LogonMessageParams logon_params,
                                                                                                                               std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref);
            boost::asio::awaitable<std::pair<boltprotocol::BoltError, boltprotocol::SuccessMessageParams>> perform_logoff_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& async_stream_variant_ref);

            // --- Synchronous IO and Chunking ---
            boltprotocol::BoltError _write_to_active_sync_stream(const uint8_t* data, size_t size);
            boltprotocol::BoltError _read_from_active_sync_stream(uint8_t* buffer, size_t size_to_read, size_t& bytes_read);
            boltprotocol::BoltError _send_chunked_payload_sync(const std::vector<uint8_t>& payload);
            boltprotocol::BoltError _receive_chunked_payload_sync(std::vector<uint8_t>& out_payload);

            // --- Asynchronous IO (Instance Members) ---
            // These are used by the instance's async lifecycle stages (_stage_..._async methods)
            // The stream_variant_ref here refers to a stream managed *during* the async establishment process,
            // which will eventually be moved into ActiveAsyncStreamContext.
            boost::asio::awaitable<boltprotocol::BoltError> _write_to_active_async_stream(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& stream_variant_ref,
                                                                                          const std::vector<uint8_t>& data);  // Pass data by const ref

            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> _read_from_active_async_stream(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& stream_variant_ref, size_t size_to_read);

            // --- Asynchronous Chunking (Instance Members) ---
            // These use the _write/_read_to_active_async_stream instance members above.
            boost::asio::awaitable<boltprotocol::BoltError> _send_chunked_payload_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& stream_variant_ref,
                                                                                        std::vector<uint8_t> payload);  // Pass payload by value for move

            boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::vector<uint8_t>>> _receive_chunked_payload_async(std::variant<boost::asio::ip::tcp::socket*, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>*>& stream_variant_ref);

            // --- Message Processing Helpers ---
            boltprotocol::BoltError _peek_message_tag(const std::vector<uint8_t>& payload, boltprotocol::MessageTag& out_tag) const;

            // --- State Management ---
            void _reset_resources_and_state(bool called_from_destructor = false);
            void _update_metadata_from_hello_success(const boltprotocol::SuccessMessageParams& meta);
            void _update_metadata_from_logon_success(const boltprotocol::SuccessMessageParams& meta);
            boltprotocol::BoltError _classify_and_set_server_failure(const boltprotocol::FailureMessageParams& meta);
            std::string _get_current_state_as_string() const;

            // --- Member Variables ---
            uint64_t id_;
            BoltConnectionConfig conn_config_;
            boost::asio::io_context& io_context_ref_;
            std::shared_ptr<spdlog::logger> logger_;

            std::unique_ptr<boost::asio::ip::tcp::socket> owned_socket_for_sync_plain_;
            std::unique_ptr<boost::asio::ip::tcp::iostream> plain_iostream_wrapper_;
            std::unique_ptr<boost::asio::ssl::context> ssl_context_sync_;
            std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ssl_stream_sync_;

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

#endif