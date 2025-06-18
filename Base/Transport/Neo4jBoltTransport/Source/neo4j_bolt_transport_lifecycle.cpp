#include <algorithm>
#include <chrono>
#include <iostream>  // 调试用

#include "boltprotocol/bolt_errors_versions.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"
#include "neo4j_bolt_transport/uri/uri_parser.h"

namespace neo4j_bolt_transport {

    // Static private helper method definition
    std::string Neo4jBoltTransport::_make_routing_context_key(const std::string& database_name, const std::optional<std::string>& impersonated_user) {
        std::string db_part = database_name.empty() ? "system" : database_name;
        if (impersonated_user && !impersonated_user->empty()) {
            return db_part + "@" + *impersonated_user;
        }
        return db_part;
    }

    Neo4jBoltTransport::Neo4jBoltTransport(config::TransportConfig a_config) : config_(std::move(a_config)) {
        if (!config_.logger) {
            config_.logger = config_.get_or_create_logger();
            if (!config_.logger) {
                std::cerr << "CRITICAL ERROR: Logger creation/retrieval failed during Neo4jBoltTransport initialization!" << std::endl;
                throw std::runtime_error("Logger initialization failed for Neo4jBoltTransport.");
            }
        }

        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport initializing with URI: '{}'", config_.uri_string);

        if (uri::UriParser::parse(config_.uri_string, parsed_initial_uri_) != boltprotocol::BoltError::SUCCESS) {
            if (config_.client_side_routing_enabled && config_.initial_router_addresses_override.empty()) {
                if (config_.logger) config_.logger->error("[TransportLC] URI '{}' parsing failed and no initial router override provided. Routing may not work.", config_.uri_string);
            } else if (config_.logger) {
                config_.logger->warn("[TransportLC] URI '{}' parsing failed, but routing is disabled or initial router override is provided.", config_.uri_string);
            }
        } else {
            if (config_.client_side_routing_enabled && config_.initial_router_addresses_override.empty() && parsed_initial_uri_.is_routing_scheme && !parsed_initial_uri_.hosts_with_ports.empty()) {
                std::vector<routing::ServerAddress> initial_routers_from_uri;
                for (const auto& hp : parsed_initial_uri_.hosts_with_ports) {
                    initial_routers_from_uri.emplace_back(hp.first, hp.second);
                }
                std::string default_context_key = _make_routing_context_key("", std::nullopt);
                config_.initial_router_addresses_override[default_context_key] = initial_routers_from_uri;
                if (config_.logger) config_.logger->info("[TransportLC] Set {} initial routers from URI for context '{}'.", initial_routers_from_uri.size(), default_context_key);
            }
        }

        config_.prepare_agent_strings();
        finalized_user_agent_ = config_.user_agent_override.empty() ? config_.bolt_agent_info.product : config_.user_agent_override;
        finalized_bolt_agent_info_ = config_.bolt_agent_info;

        if (io_context_.stopped()) {
            io_context_.restart();
        }
        work_guard_ = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(io_context_));

        if (config_.idle_timeout_ms > 0 || config_.max_connection_lifetime_ms > 0) {
            eviction_timer_ = std::make_unique<boost::asio::steady_timer>(io_context_);

            uint32_t first_eviction_delay_ms = 10000;
            if (config_.idle_timeout_ms > 0) {
                first_eviction_delay_ms = config_.idle_timeout_ms / 2;
            } else if (config_.max_connection_lifetime_ms > 0) {
                first_eviction_delay_ms = config_.max_connection_lifetime_ms / 4;
            }
            first_eviction_delay_ms = std::max(1000u, first_eviction_delay_ms);

            eviction_timer_->expires_after(std::chrono::milliseconds(first_eviction_delay_ms));
            eviction_timer_->async_wait([this](const boost::system::error_code& ec_lambda) {
                if (ec_lambda != boost::asio::error::operation_aborted && !closing_.load(std::memory_order_relaxed)) {
                    _evict_stale_connections_task();
                }
            });
            if (config_.logger) config_.logger->info("[TransportLC] Connection eviction task scheduled in {}ms.", first_eviction_delay_ms);
        }
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport initialized.");
    }

    Neo4jBoltTransport::~Neo4jBoltTransport() {
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport destructing.");
        close();
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport destruction complete.");
    }

    void Neo4jBoltTransport::close() {
        bool already_closing = closing_.exchange(true);
        if (already_closing) {
            if (config_.logger) config_.logger->debug("[TransportLC] Close already called or in progress.");
            return;
        }
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport closing...");

        if (eviction_timer_) {
            try {
                std::size_t cancelled_count = eviction_timer_->cancel();  // No error_code parameter
                if (config_.logger) {
                    config_.logger->trace("[TransportLC] Eviction timer cancelled {} pending operations.", cancelled_count);
                }
            } catch (const boost::system::system_error& e) {
                if (config_.logger) {
                    config_.logger->warn("[TransportLC] Exception during eviction_timer_.cancel(): {}", e.what());
                }
            }
            eviction_timer_.reset();
            if (config_.logger) config_.logger->trace("[TransportLC] Eviction timer reset.");
        }

        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            if (config_.logger) config_.logger->debug("[TransportLC] Terminating {} idle connections.", idle_connections_.size());
            for (auto& conn_ptr : idle_connections_) {
                if (conn_ptr) {
                    conn_ptr->terminate(true);
                }
            }
            idle_connections_.clear();
            total_connections_currently_pooled_ = 0;
        }
        pool_condition_.notify_all();

        {
            std::lock_guard<std::mutex> lock(routing_table_mutex_);
            routing_tables_.clear();
            if (config_.logger) config_.logger->debug("[TransportLC] Routing tables cleared.");
        }

        if (work_guard_) {
            work_guard_->reset();
        }
        if (!io_context_.stopped()) {
            io_context_.stop();
        }

        if (own_io_thread_ && io_thread_ && io_thread_->joinable()) {
            if (config_.logger) config_.logger->debug("[TransportLC] Joining IO thread...");
            try {
                io_thread_->join();
                if (config_.logger) config_.logger->debug("[TransportLC] IO thread joined.");
            } catch (const std::system_error& e) {
                if (config_.logger) config_.logger->error("[TransportLC] Error joining IO thread: {}", e.what());
            }
        }
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport closed.");
    }

    boltprotocol::BoltError Neo4jBoltTransport::verify_connectivity() {
        if (closing_.load(std::memory_order_acquire)) {
            if (config_.logger) config_.logger->warn("[TransportVerify] Attempting to verify connectivity on a closing transport.");
            return boltprotocol::BoltError::UNKNOWN_ERROR;
        }

        if (config_.logger) config_.logger->info("[TransportVerify] Verifying connectivity...");

        routing::ServerAddress address_to_verify;
        bool use_routing_for_verify = config_.client_side_routing_enabled && (parsed_initial_uri_.scheme != "bolt" && parsed_initial_uri_.scheme != "bolt+s" && parsed_initial_uri_.scheme != "bolt+ssc");

        if (use_routing_for_verify) {
            auto [addr_err, router_addr] = _get_server_address_for_session(config::SessionParameters{}.with_database("system"), routing::ServerRole::ROUTER);
            if (addr_err != boltprotocol::BoltError::SUCCESS || router_addr.host.empty()) {
                if (config_.logger) config_.logger->warn("[TransportVerify] Failed to get a router address for verification. Error: {}. Falling back to initial URI if possible.", neo4j_bolt_transport::error::bolt_error_to_string(addr_err));
                if (!parsed_initial_uri_.hosts_with_ports.empty()) {
                    const auto& hp = parsed_initial_uri_.hosts_with_ports.front();
                    address_to_verify = routing::ServerAddress(hp.first, hp.second);
                    if (config_.logger) config_.logger->debug("[TransportVerify] Using direct address from URI for verification: {}", address_to_verify.to_string());
                } else {
                    if (config_.logger) config_.logger->error("[TransportVerify] Connectivity verification failed: No router available and no direct address in URI.");
                    return boltprotocol::BoltError::NETWORK_ERROR;
                }
            } else {
                address_to_verify = router_addr;
            }
        } else {
            if (parsed_initial_uri_.hosts_with_ports.empty()) {
                if (config_.logger) config_.logger->error("[TransportVerify] Connectivity verification failed: No direct address in URI for non-routing scheme.");
                return boltprotocol::BoltError::INVALID_ARGUMENT;
            }
            const auto& hp = parsed_initial_uri_.hosts_with_ports.front();
            address_to_verify = routing::ServerAddress(hp.first, hp.second);
        }

        if (address_to_verify.host.empty()) {
            if (config_.logger) config_.logger->error("[TransportVerify] Connectivity verification failed: Final address to verify is empty.");
            return boltprotocol::BoltError::INVALID_ARGUMENT;
        }

        routing::ServerAddress resolved_address_to_verify = address_to_verify;
        if (config_.server_address_resolver) {
            resolved_address_to_verify = config_.server_address_resolver(address_to_verify);
        }

        if (config_.logger) config_.logger->debug("[TransportVerify] Attempting to acquire connection to {} (resolved from {}) for verification.", resolved_address_to_verify.to_string(), address_to_verify.to_string());

        auto [conn_err, conn] = _acquire_pooled_connection(resolved_address_to_verify, std::nullopt);

        if (conn_err != boltprotocol::BoltError::SUCCESS || !conn) {
            if (config_.logger) config_.logger->error("[TransportVerify] Failed to acquire connection to {} for verification. Error: {}", resolved_address_to_verify.to_string(), neo4j_bolt_transport::error::bolt_error_to_string(conn_err));
            return conn_err;
        }

        if (config_.logger) config_.logger->info("[TransportVerify] Connectivity to {} verified successfully (connection {} acquired).", resolved_address_to_verify.to_string(), conn->get_id());
        release_connection(std::move(conn), true);
        return boltprotocol::BoltError::SUCCESS;
    }

    internal::BoltConnectionConfig Neo4jBoltTransport::_create_physical_connection_config(const routing::ServerAddress& target_address, const std::optional<std::map<std::string, boltprotocol::Value>>& routing_context_for_hello) const {
        internal::BoltConnectionConfig physical_conf;
        physical_conf.target_host = target_address.host;
        physical_conf.target_port = target_address.port;
        physical_conf.auth_token = config_.auth_token;
        physical_conf.user_agent_for_hello = finalized_user_agent_;
        physical_conf.bolt_agent_info_for_hello = finalized_bolt_agent_info_;

        physical_conf.resolved_encryption_strategy = config_.encryption_strategy;
        if (config_.encryption_strategy != config::TransportConfig::EncryptionStrategy::FORCE_PLAINTEXT) {
            physical_conf.encryption_enabled = true;
            physical_conf.trusted_certificates_pem_files = config_.trusted_certificates_pem_files;
            physical_conf.client_certificate_pem_file = config_.client_certificate_pem_file;
            physical_conf.client_private_key_pem_file = config_.client_private_key_pem_file;
            physical_conf.client_private_key_password = config_.client_private_key_password;
            physical_conf.hostname_verification_enabled = config_.hostname_verification_enabled;
        } else {
            physical_conf.encryption_enabled = false;
        }

        physical_conf.tcp_connect_timeout_ms = config_.tcp_connect_timeout_ms;
        physical_conf.socket_read_timeout_ms = config_.socket_read_timeout_ms;
        physical_conf.socket_write_timeout_ms = config_.socket_write_timeout_ms;
        physical_conf.socket_keep_alive_enabled = config_.tcp_keep_alive_enabled;
        physical_conf.tcp_no_delay_enabled = config_.tcp_no_delay_enabled;
        physical_conf.bolt_handshake_timeout_ms = config_.hello_timeout_ms;  // Note: This was hello_timeout_ms, perhaps should be a dedicated handshake_timeout_ms in TransportConfig?
        physical_conf.hello_timeout_ms = config_.hello_timeout_ms;
        physical_conf.goodbye_timeout_ms = config_.goodbye_timeout_ms;

        if (routing_context_for_hello.has_value()) {
            physical_conf.hello_routing_context = routing_context_for_hello;
        }

        if (!config_.preferred_bolt_versions.empty()) {
            physical_conf.preferred_bolt_versions = config_.preferred_bolt_versions;
        }

        if (config_.logger) {
            std::string preferred_versions_str = "default";
            if (physical_conf.preferred_bolt_versions.has_value() && !physical_conf.preferred_bolt_versions->empty()) {
                preferred_versions_str.clear();
                for (const auto& v : physical_conf.preferred_bolt_versions.value()) {
                    preferred_versions_str += v.to_string() + " ";
                }
            }
            config_.logger->trace("[TransportLC] Created physical connection config: Host={}, Port={}, Enc={}, Strategy={}, ReadTimeout={}, WriteTimeout={}, HelloTimeout={}, TCPNoDelay={}, HelloRoutingCtx={}, PreferredBoltVersions=[{}]",
                                  physical_conf.target_host,
                                  physical_conf.target_port,
                                  physical_conf.encryption_enabled,
                                  static_cast<int>(physical_conf.resolved_encryption_strategy),
                                  physical_conf.socket_read_timeout_ms,
                                  physical_conf.socket_write_timeout_ms,
                                  physical_conf.hello_timeout_ms,
                                  physical_conf.tcp_no_delay_enabled,
                                  physical_conf.hello_routing_context.has_value() ? "Yes" : "No",
                                  preferred_versions_str);
        }
        return physical_conf;
    }

}  // namespace neo4j_bolt_transport