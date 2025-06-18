#include <atomic>
#include <boost/asio/detached.hpp>
#include <iostream>

#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"
#ifdef __has_include
#if __has_include(<openssl/ssl.h>)
#include <openssl/ssl.h>
#define HAS_OPENSSL_SSL_H_ASYNC_SESSION
#endif
#endif

namespace neo4j_bolt_transport {

    AsyncSessionHandle::AsyncSessionHandle(Neo4jBoltTransport* transport_mgr, config::SessionParameters params, std::unique_ptr<internal::ActiveAsyncStreamContext> stream_ctx)
        : transport_manager_(transport_mgr),
          session_params_(std::move(params)),
          stream_context_(std::move(stream_ctx)),
          current_bookmarks_(session_params_.initial_bookmarks),  // Initialize from session params
          is_closed_(false),
          close_initiated_(false),
          in_explicit_transaction_(false)  // Initialize in_explicit_transaction_
    {
        if (!transport_manager_) {
            last_error_code_ = boltprotocol::BoltError::INVALID_ARGUMENT;
            last_error_message_ = "AsyncSessionHandle created with null transport_manager.";
            is_closed_.store(true, std::memory_order_release);
            close_initiated_.store(true, std::memory_order_release);
            std::cerr << "CRITICAL: " << last_error_message_ << std::endl;
        } else if (!stream_context_ || !std::visit(
                                           [](auto& s) {
                                               return s.lowest_layer().is_open();
                                           },
                                           stream_context_->stream)) {
            last_error_code_ = boltprotocol::BoltError::NETWORK_ERROR;
            last_error_message_ = "AsyncSessionHandle created with invalid or closed stream_context.";
            is_closed_.store(true, std::memory_order_release);
            close_initiated_.store(true, std::memory_order_release);
            if (transport_manager_->get_config().logger) {
                transport_manager_->get_config().logger->error("[AsyncSessionLC] {}", last_error_message_);
            }
        } else {
            if (transport_manager_->get_config().logger) {
                transport_manager_->get_config().logger->debug("[AsyncSessionLC] AsyncSessionHandle created for DB '{}', server '{}', conn_id '{}'. Initial bookmarks: {}",
                                                               session_params_.database_name.value_or("<default>"),
                                                               stream_context_->server_agent_string,
                                                               stream_context_->server_connection_id,
                                                               current_bookmarks_.empty() ? "<none>" : std::to_string(current_bookmarks_.size()) + " items");
            }
        }
    }

    // ... (析构函数, 移动构造/赋值, mark_closed, is_valid, send_goodbye_if_appropriate_async, close_async 保持不变) ...
    AsyncSessionHandle::~AsyncSessionHandle() {
        if (!is_closed_.load(std::memory_order_acquire)) {
            std::shared_ptr<spdlog::logger> logger = nullptr;
            if (transport_manager_ && transport_manager_->get_config().logger) {
                logger = transport_manager_->get_config().logger;
            }
            if (logger) {
                logger->warn("[AsyncSessionLC] AsyncSessionHandle destructed without explicit close_async(). Forcing synchronous-like closure for stream context '{}'. This might block if called from non-asio thread or lead to issues.",
                             stream_context_ ? stream_context_->server_connection_id : "N/A");
            }
            if (stream_context_ && std::visit(
                                       [](auto& s_variant_ref) {
                                           return s_variant_ref.lowest_layer().is_open();
                                       },
                                       stream_context_->stream)) {
                std::visit(
                    [](auto& s_variant_ref) {
                        boost::system::error_code ec;
                        if constexpr (std::is_same_v<std::decay_t<decltype(s_variant_ref)>, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>) {
                        }
                        s_variant_ref.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                        s_variant_ref.lowest_layer().close(ec);
                    },
                    stream_context_->stream);
            }
            is_closed_.store(true, std::memory_order_release);
            close_initiated_.store(true, std::memory_order_release);
        }
    }

    AsyncSessionHandle::AsyncSessionHandle(AsyncSessionHandle&& other) noexcept
        : transport_manager_(other.transport_manager_),
          session_params_(std::move(other.session_params_)),
          stream_context_(std::move(other.stream_context_)),
          current_bookmarks_(std::move(other.current_bookmarks_)),
          is_closed_(other.is_closed_.load(std::memory_order_acquire)),
          close_initiated_(other.close_initiated_.load(std::memory_order_acquire)),
          in_explicit_transaction_(other.in_explicit_transaction_.load(std::memory_order_acquire)),
          last_tx_run_qid_(other.last_tx_run_qid_),
          last_error_code_(other.last_error_code_),
          last_error_message_(std::move(other.last_error_message_)) {
        other.transport_manager_ = nullptr;
        other.is_closed_.store(true, std::memory_order_release);
        other.close_initiated_.store(true, std::memory_order_release);
        other.in_explicit_transaction_.store(false, std::memory_order_release);
    }

    AsyncSessionHandle& AsyncSessionHandle::operator=(AsyncSessionHandle&& other) noexcept {
        if (this != &other) {
            if (!is_closed_.load(std::memory_order_acquire) && stream_context_) {
                std::visit(
                    [](auto& s_variant_ref) {
                        boost::system::error_code ec;
                        if constexpr (std::is_same_v<std::decay_t<decltype(s_variant_ref)>, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>) {
                        }
                        s_variant_ref.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                        s_variant_ref.lowest_layer().close(ec);
                    },
                    stream_context_->stream);
            }

            transport_manager_ = other.transport_manager_;
            session_params_ = std::move(other.session_params_);
            stream_context_ = std::move(other.stream_context_);
            current_bookmarks_ = std::move(other.current_bookmarks_);
            is_closed_.store(other.is_closed_.load(std::memory_order_acquire), std::memory_order_release);
            close_initiated_.store(other.close_initiated_.load(std::memory_order_acquire), std::memory_order_release);
            in_explicit_transaction_.store(other.in_explicit_transaction_.load(std::memory_order_acquire), std::memory_order_release);
            last_tx_run_qid_ = other.last_tx_run_qid_;
            last_error_code_ = other.last_error_code_;
            last_error_message_ = std::move(other.last_error_message_);

            other.transport_manager_ = nullptr;
            other.is_closed_.store(true, std::memory_order_release);
            other.close_initiated_.store(true, std::memory_order_release);
            other.in_explicit_transaction_.store(false, std::memory_order_release);
        }
        return *this;
    }

    void AsyncSessionHandle::mark_closed() {
        is_closed_.store(true, std::memory_order_release);
        close_initiated_.store(true, std::memory_order_release);
    }

    bool AsyncSessionHandle::is_valid() const {
        return !is_closed_.load(std::memory_order_acquire) && stream_context_ != nullptr &&
               std::visit(
                   [](const auto& s) {
                       return s.lowest_layer().is_open();
                   },
                   stream_context_->stream);
    }

    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::send_goodbye_if_appropriate_async() {
        if (!is_valid() || !stream_context_) {
            co_return boltprotocol::BoltError::SUCCESS;
        }
        if (!transport_manager_) {
            co_return boltprotocol::BoltError::INVALID_ARGUMENT;
        }

        if (stream_context_->negotiated_bolt_version < boltprotocol::versions::V3_0) {  // Corrected: Version(3,0)
            co_return boltprotocol::BoltError::SUCCESS;
        }

        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }
        if (logger) logger->trace("[AsyncSessionLC] Sending GOODBYE for connection id '{}'", stream_context_->server_connection_id);

        auto goodbye_error_handler = [this, logger_copy = logger](boltprotocol::BoltError reason, const std::string& message) {
            if (logger_copy) {
                logger_copy->warn("[AsyncSessionLC] Error during static GOODBYE send for conn_id '{}': {} - {}", stream_context_ ? stream_context_->server_connection_id : "N/A", static_cast<int>(reason), message);
            }
        };

        co_return co_await internal::BoltPhysicalConnection::send_goodbye_async_static(*stream_context_, stream_context_->original_config, logger, goodbye_error_handler);
    }

    boost::asio::awaitable<boltprotocol::BoltError> AsyncSessionHandle::close_async() {
        bool already_initiated = close_initiated_.exchange(true, std::memory_order_acq_rel);
        if (already_initiated || is_closed_.load(std::memory_order_acquire)) {
            co_return last_error_code_;
        }
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_ && transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        std::string conn_id_for_log = "N/A";
        if (stream_context_) {
            conn_id_for_log = stream_context_->server_connection_id;
        }
        if (logger) logger->debug("[AsyncSessionLC] close_async called for session with server connection id '{}'.", conn_id_for_log);

        if (is_valid() && stream_context_) {
            if (in_explicit_transaction_.load(std::memory_order_acquire)) {
                if (logger) logger->info("[AsyncSessionLC] Rolling back active async transaction during close_async for conn_id '{}'.", conn_id_for_log);
                co_await rollback_transaction_async();  // Rollback if in TX
            }
            co_await send_goodbye_if_appropriate_async();

            std::visit(
                [logger_copy = logger, log_cid = conn_id_for_log](auto& s_variant_ref) {
                    boost::system::error_code ec_shutdown, ec_close;
                    if (s_variant_ref.lowest_layer().is_open()) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(s_variant_ref)>, boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>) {
#ifdef HAS_OPENSSL_SSL_H_ASYNC_SESSION
                            if (!(SSL_get_shutdown(s_variant_ref.native_handle()) & (SSL_RECEIVED_SHUTDOWN | SSL_SENT_SHUTDOWN))) {
                                s_variant_ref.async_shutdown(boost::asio::detached);
                                if (logger_copy) logger_copy->trace("[AsyncSessionLC] Initiated async_shutdown for SSL stream (conn_id: {}).", log_cid);
                            }
#else
                            if (logger_copy) logger_copy->warn("[AsyncSessionLC] OpenSSL headers not detected for SSL_get_shutdown check (conn_id: {}). Proceeding with socket shutdown only.", log_cid);
#endif
                        }
                        s_variant_ref.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec_shutdown);
                        if (ec_shutdown && logger_copy) {
                            logger_copy->trace("[AsyncSessionLC] Socket shutdown error (conn_id: {}): {}", log_cid, ec_shutdown.message());
                        }
                        s_variant_ref.lowest_layer().close(ec_close);
                        if (ec_close && logger_copy) {
                            logger_copy->trace("[AsyncSessionLC] Socket close error (conn_id: {}): {}", log_cid, ec_close.message());
                        }
                    }
                },
                stream_context_->stream);
        }

        is_closed_.store(true, std::memory_order_release);
        stream_context_.reset();

        if (logger) logger->info("[AsyncSessionLC] AsyncSession closed (conn_id was: {}).", conn_id_for_log);
        co_return boltprotocol::BoltError::SUCCESS;
    }

    // --- Bookmark Management ---
    const std::vector<std::string>& AsyncSessionHandle::get_last_bookmarks() const {
        return current_bookmarks_;
    }

    void AsyncSessionHandle::_update_bookmarks_from_summary(const boltprotocol::SuccessMessageParams& summary_params) {
        if (is_closed_.load(std::memory_order_acquire) || !transport_manager_) {  // Added transport_manager_ check
            return;
        }
        std::shared_ptr<spdlog::logger> logger = nullptr;
        if (transport_manager_->get_config().logger) {
            logger = transport_manager_->get_config().logger;
        }

        auto it_bookmark = summary_params.metadata.find("bookmark");
        if (it_bookmark != summary_params.metadata.end() && std::holds_alternative<std::string>(it_bookmark->second)) {
            current_bookmarks_ = {std::get<std::string>(it_bookmark->second)};  // Replace with the new single bookmark
            if (logger) logger->trace("[AsyncSessionBM] Bookmarks updated from summary: {}", current_bookmarks_[0]);
        } else {
            // As per Java driver behavior, if a successful operation that could produce a bookmark
            // does not return one, existing bookmarks should be cleared.
            current_bookmarks_.clear();
            if (logger) logger->trace("[AsyncSessionBM] No bookmark in summary, bookmarks cleared.");
        }
    }

}  // namespace neo4j_bolt_transport