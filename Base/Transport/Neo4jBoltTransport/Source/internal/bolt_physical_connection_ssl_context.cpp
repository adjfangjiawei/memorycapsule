#include <boost/asio/ssl.hpp>
#include <boost/system/error_code.hpp>
#include <iostream>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_physical_connection.h"

namespace neo4j_bolt_transport {
    namespace internal {

        boltprotocol::BoltError BoltPhysicalConnection::_stage_ssl_context_setup() {
            if (!conn_config_.encryption_enabled) {
                if (logger_) logger_->debug("[ConnSSLCTX {}] SSL encryption not enabled, skipping context setup.", id_);
                return boltprotocol::BoltError::SUCCESS;
            }
            if (current_state_.load(std::memory_order_relaxed) != InternalState::TCP_CONNECTED) {
                std::string msg = "SSL context setup called but TCP not connected. Current state: " + _get_current_state_as_string();
                _mark_as_defunct_internal(boltprotocol::BoltError::UNKNOWN_ERROR, msg);  // 使用 internal
                if (logger_) logger_->error("[ConnSSLCTX {}] {}", id_, msg);
                return last_error_code_;
            }

            current_state_.store(InternalState::SSL_CONTEXT_SETUP, std::memory_order_relaxed);
            if (logger_) logger_->debug("[ConnSSLCTX {}] Setting up SSL context.", id_);

            try {
                ssl_context_sync_ = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_client);
                boost::system::error_code ec_ssl_setup;

                switch (conn_config_.resolved_encryption_strategy) {
                    case config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_TRUST_ALL_CERTS:
                        ssl_context_sync_->set_verify_mode(boost::asio::ssl::verify_none, ec_ssl_setup);
                        if (logger_ && !ec_ssl_setup) logger_->warn("[ConnSSLCTX {}] SSL configured to TRUST_ALL_CERTIFICATES (verify_none). THIS IS INSECURE.", id_);
                        break;
                    case config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_SYSTEM_CERTS:
                        ssl_context_sync_->set_default_verify_paths(ec_ssl_setup);
                        if (!ec_ssl_setup) {
                            ssl_context_sync_->set_verify_mode(boost::asio::ssl::verify_peer, ec_ssl_setup);
                        }
                        break;
                    case config::TransportConfig::EncryptionStrategy::FORCE_ENCRYPTED_CUSTOM_CERTS:
                        ssl_context_sync_->set_verify_mode(boost::asio::ssl::verify_peer, ec_ssl_setup);
                        if (!ec_ssl_setup) {
                            if (conn_config_.trusted_certificates_pem_files.empty() && logger_) {
                                logger_->warn("[ConnSSLCTX {}] SSL configured for custom CAs but no CA certificate files provided. Verification will likely fail.", id_);
                            }
                            for (const auto& cert_path : conn_config_.trusted_certificates_pem_files) {
                                ssl_context_sync_->load_verify_file(cert_path, ec_ssl_setup);
                                if (ec_ssl_setup) {
                                    if (logger_) logger_->error("[ConnSSLCTX {}] Failed to load custom CA certificate file '{}': {}", id_, cert_path, ec_ssl_setup.message());
                                    break;
                                }
                                if (logger_) logger_->debug("[ConnSSLCTX {}] Successfully loaded custom CA certificate file: {}", id_, cert_path);
                            }
                        }
                        break;
                    case config::TransportConfig::EncryptionStrategy::FORCE_PLAINTEXT:
                    case config::TransportConfig::EncryptionStrategy::NEGOTIATE_FROM_URI_SCHEME:
                    default:
                        std::string msg = "Invalid or unresolved encryption strategy for SSL context setup: " + std::to_string(static_cast<int>(conn_config_.resolved_encryption_strategy));
                        _mark_as_defunct_internal(boltprotocol::BoltError::INVALID_ARGUMENT, msg);  // 使用 internal
                        if (logger_) logger_->error("[ConnSSLCTX {}] {}", id_, msg);
                        return last_error_code_;
                }

                if (ec_ssl_setup) {
                    std::string msg = "SSL context verification setup failed: " + ec_ssl_setup.message();
                    _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用 internal
                    if (logger_) logger_->error("[ConnSSLCTX {}] {}", id_, msg);
                    return last_error_code_;
                }

                if (conn_config_.client_certificate_pem_file.has_value()) {
                    if (!conn_config_.client_private_key_pem_file.has_value()) {
                        std::string msg = "Client certificate provided, but client private key is missing.";
                        _mark_as_defunct_internal(boltprotocol::BoltError::INVALID_ARGUMENT, msg);  // 使用 internal
                        if (logger_) logger_->error("[ConnSSLCTX {}] {}", id_, msg);
                        return last_error_code_;
                    }

                    if (logger_) logger_->debug("[ConnSSLCTX {}] Attempting to load client certificate: {}", id_, conn_config_.client_certificate_pem_file.value());
                    ssl_context_sync_->use_certificate_chain_file(conn_config_.client_certificate_pem_file.value(), ec_ssl_setup);
                    if (ec_ssl_setup) {
                        std::string msg = "Failed to load client certificate chain file '" + conn_config_.client_certificate_pem_file.value() + "': " + ec_ssl_setup.message();
                        _mark_as_defunct_internal(boltprotocol::BoltError::INVALID_ARGUMENT, msg);  // 使用 internal
                        if (logger_) logger_->error("[ConnSSLCTX {}] {}", id_, msg);
                        return last_error_code_;
                    }

                    if (logger_) logger_->debug("[ConnSSLCTX {}] Attempting to load client private key: {}", id_, conn_config_.client_private_key_pem_file.value());
                    if (conn_config_.client_private_key_password.has_value() && !conn_config_.client_private_key_password.value().empty()) {
                        ssl_context_sync_->set_password_callback(
                            [pwd = conn_config_.client_private_key_password.value()](std::size_t, boost::asio::ssl::context_base::password_purpose) {
                                return pwd;
                            },
                            ec_ssl_setup);
                        if (ec_ssl_setup) {
                            std::string msg = "Failed to set password callback for client private key: " + ec_ssl_setup.message();
                            _mark_as_defunct_internal(boltprotocol::BoltError::INVALID_ARGUMENT, msg);  // 使用 internal
                            if (logger_) logger_->error("[ConnSSLCTX {}] {}", id_, msg);
                            return last_error_code_;
                        }
                    }
                    ssl_context_sync_->use_private_key_file(conn_config_.client_private_key_pem_file.value(), boost::asio::ssl::context::pem, ec_ssl_setup);
                    if (ec_ssl_setup) {
                        std::string msg = "Failed to load client private key file '" + conn_config_.client_private_key_pem_file.value() + "': " + ec_ssl_setup.message();
                        _mark_as_defunct_internal(boltprotocol::BoltError::INVALID_ARGUMENT, msg);  // 使用 internal
                        if (logger_) logger_->error("[ConnSSLCTX {}] {}", id_, msg);
                        return last_error_code_;
                    }
                    if (logger_) logger_->info("[ConnSSLCTX {}] Client certificate and private key loaded successfully for mTLS.", id_);
                }

            } catch (const boost::system::system_error& e) {
                std::string msg = "ASIO system error during SSL context setup: " + std::string(e.what());
                _mark_as_defunct_internal(boltprotocol::BoltError::NETWORK_ERROR, msg);  // 使用 internal
                if (logger_) logger_->error("[ConnSSLCTX {}] {}", id_, msg);
                return last_error_code_;
            } catch (const std::exception& e) {
                std::string msg = "Standard exception during SSL context setup: " + std::string(e.what());
                _mark_as_defunct_internal(boltprotocol::BoltError::UNKNOWN_ERROR, msg);  // 使用 internal
                if (logger_) logger_->error("[ConnSSLCTX {}] {}", id_, msg);
                return last_error_code_;
            }

            last_error_code_ = boltprotocol::BoltError::SUCCESS;
            last_error_message_.clear();
            if (logger_) logger_->debug("[ConnSSLCTX {}] SSL context setup complete.", id_);
            return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace internal
}  // namespace neo4j_bolt_transport