#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/system/error_code.hpp>
#include <iostream>  // 调试用

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

// For OpenSSL specific calls if needed (SNI, error details)
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::_stage_tcp_connect() {
            // 重置之前的流和套接字资源
            if (plain_iostream_wrapper_) plain_iostream_wrapper_.reset();
            if (ssl_stream_sync_) ssl_stream_sync_.reset();                                 // 使用 _sync_ 后缀
            if (owned_socket_for_sync_plain_ && owned_socket_for_sync_plain_->is_open()) {  // 使用 _sync_ 后缀
                boost::system::error_code ignored_ec;
                try {
                    owned_socket_for_sync_plain_->close(ignored_ec);
                } catch (...) {
                }  // 使用 _sync_ 后缀
            }
            owned_socket_for_sync_plain_.reset();              // 使用 _sync_ 后缀
            if (ssl_context_sync_) ssl_context_sync_.reset();  // 使用 _sync_ 后缀

            current_state_.store(InternalState::TCP_CONNECTING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnSock {}] TCP Connecting to {}:{}", id_, conn_config_.target_host, conn_config_.target_port);

            owned_socket_for_sync_plain_ = std::make_unique<boost::asio::ip::tcp::socket>(io_context_ref_);  // 使用 _sync_ 后缀

            try {
                boost::asio::ip::tcp::resolver resolver(io_context_ref_);
                boost::system::error_code resolve_ec;
                // TODO: 实现解析器超时
                auto endpoints = resolver.resolve(conn_config_.target_host, std::to_string(conn_config_.target_port), resolve_ec);

                if (resolve_ec) {
                    std::string msg = "DNS resolution failed for " + conn_config_.target_host + ": " + resolve_ec.message();
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                    if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                    return last_error_code_;
                }
                if (endpoints.empty()) {
                    std::string msg = "DNS resolution for " + conn_config_.target_host + " returned no endpoints.";
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                    if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                    return last_error_code_;
                }

                // TODO: 实现TCP连接超时
                boost::system::error_code connect_ec;
                boost::asio::connect(*owned_socket_for_sync_plain_, endpoints, connect_ec);  // 使用 _sync_ 后缀

                if (connect_ec) {
                    std::string msg = "TCP connect to " + conn_config_.target_host + ":" + std::to_string(conn_config_.target_port) + " failed: " + connect_ec.message();
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                    if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                    return last_error_code_;
                }

                if (conn_config_.socket_keep_alive_enabled) {
                    boost::system::error_code keep_alive_ec;
                    owned_socket_for_sync_plain_->set_option(boost::asio::socket_base::keep_alive(true), keep_alive_ec);  // 使用 _sync_ 后缀
                    if (keep_alive_ec && logger_) {
                        logger_->warn("[ConnSock {}] Failed to set SO_KEEPALIVE: {}", id_, keep_alive_ec.message());
                    }
                }
                if (conn_config_.tcp_no_delay_enabled) {  // 新增 TCP_NODELAY
                    boost::system::error_code no_delay_ec;
                    owned_socket_for_sync_plain_->set_option(boost::asio::ip::tcp::no_delay(true), no_delay_ec);  // 使用 _sync_ 后缀
                    if (no_delay_ec && logger_) {
                        logger_->warn("[ConnSock {}] Failed to set TCP_NODELAY: {}", id_, no_delay_ec.message());
                    }
                }

                if (!conn_config_.encryption_enabled) {                                               // 如果不是SSL，现在包装socket
                    if (!owned_socket_for_sync_plain_ || !owned_socket_for_sync_plain_->is_open()) {  // 使用 _sync_ 后缀
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "Socket not open for plain stream wrapper.");
                        if (logger_) logger_->error("[ConnSock {}] Socket not open for plain stream wrapper after TCP connect.", id_);
                        return last_error_code_;
                    }
                    try {
                        // 将 owned_socket_for_sync_plain_ 的所有权转移给 iostream
                        plain_iostream_wrapper_ = std::make_unique<boost::asio::ip::tcp::iostream>(std::move(*owned_socket_for_sync_plain_));
                        owned_socket_for_sync_plain_.reset();  // 确认所有权已转移
                        if (!plain_iostream_wrapper_->good()) {
                            std::string msg = "Failed to initialize plain iostream wrapper.";
                            _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                            if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                            return last_error_code_;
                        }
                    } catch (const std::exception& e) {
                        std::string msg = "Exception creating plain iostream wrapper: " + std::string(e.what());
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                        if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                        return last_error_code_;
                    }
                }

            } catch (const boost::system::system_error& e) {
                std::string msg = "ASIO system error during TCP connect: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::exception& e) {
                std::string msg = "Standard exception during TCP connect: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::OUT_OF_MEMORY, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::TCP_CONNECTED, std::memory_order_relaxed);
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->debug("[ConnSock {}] TCP connection established to {}:{}.", id_, conn_config_.target_host, conn_config_.target_port);
            return boltprotocol::BoltError::SUCCESS;
        }

        boltprotocol::BoltError BoltPhysicalConnection::_stage_ssl_context_setup() {
            if (!conn_config_.encryption_enabled) {
                return boltprotocol::BoltError::SUCCESS;
            }
            if (current_state_.load(std::memory_order_relaxed) != InternalState::TCP_CONNECTED) {
                std::string msg = "SSL context setup called but TCP not connected. State: " + _get_current_state_as_string();
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::SSL_CONTEXT_SETUP, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnSock {}] Setting up SSL context.", id_);

            try {
                ssl_context_sync_ = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_client);  // 使用 _sync_ 后缀
                boost::system::error_code ec;

                switch (conn_config_.resolved_encryption_strategy) {
                    case config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS:
                        ssl_context_sync_->set_verify_mode(boost::asio::ssl::verify_none, ec);  // 使用 _sync_ 后缀
                        if (logger_ && !ec) logger_->warn("[ConnSock {}] SSL configured to TRUST_ALL_CERTIFICATES (verify_none). THIS IS INSECURE.", id_);
                        break;
                    case config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_SYSTEM_CERTS:
                        ssl_context_sync_->set_default_verify_paths(ec);                                 // 使用 _sync_ 后缀
                        if (!ec) ssl_context_sync_->set_verify_mode(boost::asio::ssl::verify_peer, ec);  // 使用 _sync_ 后缀
                        break;
                    case config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_CUSTOM_CERTS:
                        ssl_context_sync_->set_verify_mode(boost::asio::ssl::verify_peer, ec);  // 使用 _sync_ 后缀
                        if (!ec) {
                            if (conn_config_.trusted_certificates_pem_files.empty() && logger_) {
                                logger_->warn("[ConnSock {}] SSL configured for custom CAs but no CA files provided. Verification may fail.", id_);
                            }
                            for (const auto& cert_path : conn_config_.trusted_certificates_pem_files) {
                                ssl_context_sync_->load_verify_file(cert_path, ec);  // 使用 _sync_ 后缀
                                if (ec) {
                                    if (logger_) logger_->error("[ConnSock {}] Failed to load custom CA file '{}': {}", id_, cert_path, ec.message());
                                    break;
                                }
                                if (logger_) logger_->debug("[ConnSock {}] Loaded custom CA file: {}", id_, cert_path);
                            }
                        }
                        break;
                    default:
                        std::string msg = "Invalid encryption strategy for SSL context setup: " + std::to_string(static_cast<int>(conn_config_.resolved_encryption_strategy));
                        _mark_as_defunct(boltprotocol::BoltError::INVALID_ARGUMENT, msg);
                        if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                        return last_error_code_;
                }

                if (ec) {
                    std::string msg = "SSL context verification setup failed: " + ec.message();
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                    if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                    return last_error_code_;
                }

                if (conn_config_.client_certificate_pem_file.has_value()) {
                    // ... (加载客户端证书和私钥的逻辑，确保使用 ssl_context_sync_) ...
                    if (logger_) logger_->debug("[ConnSock {}] Using client certificate: {}", id_, conn_config_.client_certificate_pem_file.value());
                    ssl_context_sync_->use_certificate_chain_file(conn_config_.client_certificate_pem_file.value(), ec);
                    if (ec) { /* ... error handling ... */
                        return last_error_code_;
                    }

                    if (conn_config_.client_private_key_pem_file.has_value()) {
                        if (logger_) logger_->debug("[ConnSock {}] Using client private key: {}", id_, conn_config_.client_private_key_pem_file.value());
                        if (conn_config_.client_private_key_password.has_value() && !conn_config_.client_private_key_password.value().empty()) {
                            ssl_context_sync_->set_password_callback(
                                [pwd = conn_config_.client_private_key_password.value()](std::size_t, boost::asio::ssl::context_base::password_purpose) {
                                    return pwd;
                                },
                                ec);
                            if (ec) { /* ... error handling ... */
                                return last_error_code_;
                            }
                        }
                        ssl_context_sync_->use_private_key_file(conn_config_.client_private_key_pem_file.value(), boost::asio::ssl::context::pem, ec);
                        if (ec) { /* ... error handling ... */
                            return last_error_code_;
                        }
                    } else { /* ... error: private key missing ... */
                        return last_error_code_;
                    }
                }

            } catch (const boost::system::system_error& e) {
                std::string msg = "ASIO system error during SSL context setup: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::exception& e) {
                std::string msg = "Standard exception during SSL context setup: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            }

            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->debug("[ConnSock {}] SSL context setup complete.", id_);
            return boltprotocol::BoltError::SUCCESS;
        }

        boltprotocol::BoltError BoltPhysicalConnection::_stage_ssl_handshake() {
            if (!conn_config_.encryption_enabled) return boltprotocol::BoltError::SUCCESS;
            if (current_state_.load(std::memory_order_relaxed) != InternalState::SSL_CONTEXT_SETUP) {
                std::string msg = "SSL handshake called in unexpected state: " + _get_current_state_as_string();
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            }
            // owned_socket_for_sync_plain_ 应该在这里是有效的，因为它在 _stage_tcp_connect 中创建并且尚未转移所有权
            if (!ssl_context_sync_ || !owned_socket_for_sync_plain_ || !owned_socket_for_sync_plain_->is_open()) {  // 使用 _sync_ 后缀
                std::string msg = "SSL handshake attempted without valid SSL context or connected TCP socket.";
                _mark_as_defunct(boltprotocol::BoltError::INVALID_ARGUMENT, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::SSL_HANDSHAKING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnSock {}] Performing SSL handshake for host {}...", id_, conn_config_.target_host);

            try {
                // 将 owned_socket_for_sync_plain_ 的所有权转移给 ssl_stream_sync_
                ssl_stream_sync_ = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(std::move(*owned_socket_for_sync_plain_), *ssl_context_sync_);  // 使用 _sync_ 后缀
                owned_socket_for_sync_plain_.reset();                                                                                                                       // 确认所有权已转移

                if (conn_config_.hostname_verification_enabled && conn_config_.resolved_encryption_strategy != config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS) {
                    // 设置SNI
                    if (!SSL_set_tlsext_host_name(ssl_stream_sync_->native_handle(), conn_config_.target_host.c_str())) {  // 使用 _sync_ 后缀
                        boost::system::error_code sni_ec(static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category());
                        std::string msg = "Failed to set SNI extension for host " + conn_config_.target_host + ": " + sni_ec.message();
                        if (logger_) logger_->warn("[ConnSock {}] {}", id_, msg);  // SNI失败通常不是致命的，但应记录
                    } else {
                        if (logger_) logger_->trace("[ConnSock {}] SNI hostname set to: {}", id_, conn_config_.target_host);
                    }
                    // 设置主机名验证回调
                    ssl_stream_sync_->set_verify_callback(boost::asio::ssl::host_name_verification(conn_config_.target_host));  // 使用 _sync_ 后缀
                }

                boost::system::error_code handshake_ec;
                ssl_stream_sync_->handshake(boost::asio::ssl::stream_base::client, handshake_ec);  // 使用 _sync_ 后缀

                if (handshake_ec) {
                    std::string msg = "SSL handshake failed for host " + conn_config_.target_host + ": " + handshake_ec.message();
                    unsigned long openssl_err_code = ERR_get_error();
                    if (openssl_err_code != 0) {
                        char err_buf[256];
                        ERR_error_string_n(openssl_err_code, err_buf, sizeof(err_buf));
                        msg += " (OpenSSL: " + std::string(err_buf) + ")";
                    }
                    _mark_as_defunct(boltprotocol::BoltError::HANDSHAKE_FAILED, msg);
                    if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                    return last_error_code_;
                }
            } catch (const boost::system::system_error& e) {
                std::string msg = "ASIO system error during SSL handshake: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::HANDSHAKE_FAILED, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::exception& e) {
                std::string msg = "Standard exception during SSL handshake: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::SSL_HANDSHAKEN, std::memory_order_relaxed);
            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->debug("[ConnSock {}] SSL handshake successful for {}.", id_, conn_config_.target_host);
            return boltprotocol::BoltError::SUCCESS;
        }

        // --- Asynchronous Socket Operations (Placeholders) ---
        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_stage_tcp_connect_async() {
            // ... 实现异步 TCP 连接 ...
            // 将使用 co_await socket.async_connect(...)
            // 重置之前的资源
            // current_state_.store(InternalState::TCP_CONNECTING, std::memory_order_relaxed);
            // ...
            // co_return error_code;
            co_return boltprotocol::BoltError::UNKNOWN_ERROR;  // Placeholder
        }

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_stage_ssl_context_setup_async() {
            // SSL 上下文设置通常是同步的，因为它不涉及 I/O
            // 但如果涉及从文件异步加载证书，则可能需要异步
            // 对于当前实现，可以认为它是同步完成的，或者直接在 establish_async 中调用同步版本
            co_return _stage_ssl_context_setup();  // 调用同步版本，或者标记为不支持纯异步上下文设置
        }

        boost::asio::awaitable<boltprotocol::BoltError> BoltPhysicalConnection::_stage_ssl_handshake_async() {
            // ... 实现异步 SSL 握手 ...
            // 将使用 co_await ssl_stream.async_handshake(...)
            // current_state_.store(InternalState::SSL_HANDSHAKING, std::memory_order_relaxed);
            // ...
            // co_return error_code;
            co_return boltprotocol::BoltError::UNKNOWN_ERROR;  // Placeholder
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport