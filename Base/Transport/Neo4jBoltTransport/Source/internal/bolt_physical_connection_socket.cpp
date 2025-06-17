#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>  // For host_name_verification if used directly
#include <boost/system/error_code.hpp>
#include <iostream>  // For debug, replace with logging

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

// For SSL_set_tlsext_host_name and ERR_get_error, ERR_error_string_n
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::_stage_tcp_connect() {
            if (plain_iostream_wrapper_) plain_iostream_wrapper_.reset();
            if (ssl_stream_) ssl_stream_.reset();
            if (owned_socket_ && owned_socket_->is_open()) {
                boost::system::error_code ignored_ec;
                owned_socket_->close(ignored_ec);
            }
            owned_socket_.reset();
            ssl_context_.reset();

            current_state_.store(InternalState::TCP_CONNECTING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnSock {}] TCP Connecting to {}:{}", id_, conn_config_.target_host, conn_config_.target_port);

            owned_socket_ = std::make_unique<boost::asio::ip::tcp::socket>(io_context_ref_);

            try {
                boost::asio::ip::tcp::resolver resolver(io_context_ref_);
                boost::system::error_code resolve_ec;
                // TODO: Implement resolver timeout (e.g., using async_resolve with a timer)
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

                // TODO: Implement actual TCP connect timeout (e.g. using async_connect with a timer)
                // For synchronous connect, a more complex setup or OS-level socket options might be needed before connect.
                // For now, using Boost.Asio's synchronous connect without explicit timeout control here.
                boost::system::error_code connect_ec;
                boost::asio::connect(*owned_socket_, endpoints, connect_ec);

                if (connect_ec) {
                    std::string msg = "TCP connect to " + conn_config_.target_host + ":" + std::to_string(conn_config_.target_port) + " failed: " + connect_ec.message();
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                    if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                    return last_error_code_;
                }

                if (conn_config_.socket_keep_alive_enabled) {
                    boost::system::error_code keep_alive_ec;
                    owned_socket_->set_option(boost::asio::socket_base::keep_alive(true), keep_alive_ec);
                    if (keep_alive_ec && logger_) {
                        logger_->warn("[ConnSock {}] Failed to set SO_KEEPALIVE: {}", id_, keep_alive_ec.message());
                    }
                }
                // If not using SSL, wrap the socket in plain_iostream_wrapper_ now
                if (!conn_config_.encryption_enabled) {
                    if (!owned_socket_ || !owned_socket_->is_open()) {  // Should not happen if connect succeeded
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, "Socket not open for plain stream wrapper.");
                        if (logger_) logger_->error("[ConnSock {}] Socket not open for plain stream wrapper after TCP connect.", id_);
                        return last_error_code_;
                    }
                    try {
                        plain_iostream_wrapper_ = std::make_unique<boost::asio::ip::tcp::iostream>(std::move(*owned_socket_));
                        owned_socket_.reset();  // Ownership transferred
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
            } catch (const std::exception& e) {  // Catch other potential exceptions like bad_alloc
                std::string msg = "Standard exception during TCP connect: " + std::string(e.what());
                _mark_as_defunct(boltprotocol::BoltError::OUT_OF_MEMORY, msg);  // Assuming std::bad_alloc or similar
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
                // TODO: Allow configuration of TLS versions (e.g. tlsv1_3_client)
                ssl_context_ = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_client);
                boost::system::error_code ec;

                // Set options for SNI (Server Name Indication)
                // SSL_CTX_set_tlsext_servername_callback(ssl_context_->native_handle(), nullptr); // Not needed for client
                // SSL_CTX_set_tlsext_servername_arg(ssl_context_->native_handle(), nullptr);      // Not needed for client

                switch (conn_config_.resolved_encryption_strategy) {
                    case config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS:
                        ssl_context_->set_verify_mode(boost::asio::ssl::verify_none, ec);
                        if (logger_ && !ec) logger_->warn("[ConnSock {}] SSL configured to TRUST_ALL_CERTIFICATES (verify_none). THIS IS INSECURE.", id_);
                        break;
                    case config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_SYSTEM_CERTS:
                        ssl_context_->set_default_verify_paths(ec);
                        if (!ec) ssl_context_->set_verify_mode(boost::asio::ssl::verify_peer, ec);
                        break;
                    case config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_CUSTOM_CERTS:
                        ssl_context_->set_verify_mode(boost::asio::ssl::verify_peer, ec);  // verify_peer is essential
                        if (!ec) {
                            if (conn_config_.trusted_certificates_pem_files.empty() && logger_) {
                                logger_->warn("[ConnSock {}] SSL configured for custom CAs but no CA files provided. Verification may fail.", id_);
                            }
                            for (const auto& cert_path : conn_config_.trusted_certificates_pem_files) {
                                ssl_context_->load_verify_file(cert_path, ec);
                                if (ec) {
                                    if (logger_) logger_->error("[ConnSock {}] Failed to load custom CA file '{}': {}", id_, cert_path, ec.message());
                                    break;  // Exit loop on first error
                                }
                                if (logger_) logger_->debug("[ConnSock {}] Loaded custom CA file: {}", id_, cert_path);
                            }
                        }
                        break;
                    default:  // Should include NEGOTIATE_FROM_URI_SCHEME if it resolved to an unknown/plaintext state for SSL setup
                        std::string msg = "Invalid encryption strategy for SSL context setup: " + std::to_string(static_cast<int>(conn_config_.resolved_encryption_strategy));
                        _mark_as_defunct(boltprotocol::BoltError::INVALID_ARGUMENT, msg);
                        if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                        return last_error_code_;
                }

                if (ec) {  // Check error code after switch
                    std::string msg = "SSL context verification setup failed: " + ec.message();
                    _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);  // More specific error? HANDSHAKE_FAILED?
                    if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                    return last_error_code_;
                }

                // Hostname verification (if not TRUST_ALL_CERTS)
                if (conn_config_.hostname_verification_enabled && conn_config_.resolved_encryption_strategy != config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS) {
                    // Boost.ASIO's verify_peer with a correctly set servername for SNI typically handles this.
                    // For explicit control or older versions:
                    // ssl_context_->set_verify_callback(boost::asio::ssl::host_name_verification(conn_config_.target_host), ec);
                    // if (ec && logger_) { logger_->warn("[ConnSock {}] Failed to set SSL hostname verification callback: {}", id_, ec.message()); }
                    // Modern approach: ensure SNI is set on the stream before handshake.
                }

                // Client certificate (mutual TLS)
                if (conn_config_.client_certificate_pem_file.has_value()) {
                    if (logger_) logger_->debug("[ConnSock {}] Using client certificate: {}", id_, conn_config_.client_certificate_pem_file.value());
                    ssl_context_->use_certificate_chain_file(conn_config_.client_certificate_pem_file.value(), ec);
                    if (ec) {
                        std::string msg = "Failed to load client certificate chain file: " + ec.message();
                        _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                        if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                        return last_error_code_;
                    }

                    if (conn_config_.client_private_key_pem_file.has_value()) {
                        if (logger_) logger_->debug("[ConnSock {}] Using client private key: {}", id_, conn_config_.client_private_key_pem_file.value());
                        if (conn_config_.client_private_key_password.has_value() && !conn_config_.client_private_key_password.value().empty()) {
                            ssl_context_->set_password_callback(
                                [pwd = conn_config_.client_private_key_password.value()](std::size_t /*max_len*/, boost::asio::ssl::context_base::password_purpose /*purpose*/) {
                                    return pwd;
                                },
                                ec);
                            if (ec) {
                                std::string msg = "Failed to set SSL password callback for private key: " + ec.message();
                                _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                                return last_error_code_;
                            }
                        }
                        ssl_context_->use_private_key_file(conn_config_.client_private_key_pem_file.value(), boost::asio::ssl::context::pem, ec);
                        if (ec) {
                            std::string msg = "Failed to load client private key file: " + ec.message();
                            _mark_as_defunct(boltprotocol::BoltError::NETWORK_ERROR, msg);
                            if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                            return last_error_code_;
                        }
                    } else {
                        // Standard practice: if client cert is provided, private key must also be provided.
                        std::string msg = "Client certificate provided but no client private key file specified.";
                        _mark_as_defunct(boltprotocol::BoltError::INVALID_ARGUMENT, msg);
                        if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
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
                _mark_as_defunct(boltprotocol::BoltError::UNKNOWN_ERROR, msg);  // Or more specific if identifiable
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
            if (!ssl_context_ || !owned_socket_ || !owned_socket_->is_open()) {
                std::string msg = "SSL handshake attempted without valid SSL context or connected TCP socket.";
                _mark_as_defunct(boltprotocol::BoltError::INVALID_ARGUMENT, msg);
                if (logger_) logger_->error("[ConnSock {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::SSL_HANDSHAKING, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnSock {}] Performing SSL handshake for host {}...", id_, conn_config_.target_host);

            try {
                ssl_stream_ = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(std::move(*owned_socket_), *ssl_context_);
                owned_socket_.reset();  // Ownership transferred

                // Set SNI (Server Name Indication) and Hostname Verification
                if (conn_config_.hostname_verification_enabled && conn_config_.resolved_encryption_strategy != config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS) {
                    // Set SNI for the handshake using OpenSSL directly via native_handle
                    // This is important for the server to pick the correct certificate.
                    if (!SSL_set_tlsext_host_name(ssl_stream_->native_handle(), conn_config_.target_host.c_str())) {
                        boost::system::error_code sni_ec(static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category());
                        std::string msg = "Failed to set SNI extension for host " + conn_config_.target_host + ": " + sni_ec.message();
                        // This might not be fatal depending on server config, but log a warning.
                        if (logger_) logger_->warn("[ConnSock {}] {}", id_, msg);
                    } else {
                        if (logger_) logger_->trace("[ConnSock {}] SNI hostname set to: {}", id_, conn_config_.target_host);
                    }
                    // For hostname verification against the certificate:
                    // Boost.ASIO >= 1.70.0 can use stream params for this.
                    // Otherwise, rely on verify_callback set on context or default OpenSSL behavior.
                    // For this example, we assume `verify_peer` and SNI is sufficient for most cases with modern OpenSSL.
                    // Explicit boost::asio::ssl::host_name_verification(target_host) can be set as verify_callback on context.
                    ssl_stream_->set_verify_callback(boost::asio::ssl::host_name_verification(conn_config_.target_host));
                }

                // TODO: Implement SSL handshake timeout (e.g., using async_handshake with a timer)
                boost::system::error_code handshake_ec;
                ssl_stream_->handshake(boost::asio::ssl::stream_base::client, handshake_ec);

                if (handshake_ec) {
                    std::string msg = "SSL handshake failed for host " + conn_config_.target_host + ": " + handshake_ec.message();
                    // Get more detailed OpenSSL error
                    unsigned long openssl_err_code = ERR_get_error();  // Get the earliest error on the queue
                    std::string openssl_err_str;
                    if (openssl_err_code != 0) {
                        char err_buf[256];
                        ERR_error_string_n(openssl_err_code, err_buf, sizeof(err_buf));
                        openssl_err_str = std::string(err_buf);
                        msg += " (OpenSSL: " + openssl_err_str + ")";
                    }

                    _mark_as_defunct(boltprotocol::BoltError::HANDSHAKE_FAILED, msg);  // More specific error
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

        // Removed _get_plain_input_stream and _get_plain_output_stream as they are not declared in the header
        // and direct stream access for chunkers is handled by passing the stream wrapper itself.

    }  // namespace internal
}  // namespace neo4j_bolt_transport