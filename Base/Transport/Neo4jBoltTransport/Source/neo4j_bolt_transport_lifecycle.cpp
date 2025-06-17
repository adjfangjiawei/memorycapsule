#include <algorithm>  // For std::max
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>    // 仍然需要，因为 async_wait 的回调会用到
#include <boost/system/system_error.hpp>  // 用于捕获 cancel() 可能抛出的异常
#include <iostream>
#include <stdexcept>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"
#include "neo4j_bolt_transport/uri/uri_parser.h"

namespace neo4j_bolt_transport {

    // ... (Neo4jBoltTransport::_make_routing_context_key 定义) ...
    std::string Neo4jBoltTransport::_make_routing_context_key(const std::string& database_name, const std::optional<std::string>& impersonated_user) {
        std::string db_part = database_name.empty() ? "system" : database_name;
        if (impersonated_user && !impersonated_user->empty()) {
            return db_part + "@" + *impersonated_user;
        }
        return db_part;
    }

    // ... (Neo4jBoltTransport 构造函数实现，保持之前的修复) ...
    Neo4jBoltTransport::Neo4jBoltTransport(config::TransportConfig a_config) : config_(std::move(a_config)) {
        if (!config_.logger) {
            config_.logger = config_.get_or_create_logger();
            if (!config_.logger) {
                std::cerr << "严重错误: Neo4jBoltTransport 初始化期间无法创建或获取 logger！" << std::endl;
                throw std::runtime_error("Logger initialization failed for Neo4jBoltTransport.");
            }
        }

        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport 正在初始化，URI: '{}'", config_.uri_string);

        if (uri::UriParser::parse(config_.uri_string, parsed_initial_uri_) != boltprotocol::BoltError::SUCCESS) {
            if (config_.client_side_routing_enabled && config_.initial_router_addresses_override.empty()) {
                if (config_.logger) config_.logger->error("[TransportLC] URI '{}' 解析失败且未提供初始路由器覆盖，路由可能无法工作。", config_.uri_string);
                throw std::runtime_error("无效的 Neo4j URI 且未配置初始路由器: " + config_.uri_string);
            } else if (config_.logger) {
                config_.logger->warn("[TransportLC] URI '{}' 解析失败，但路由未启用或已提供初始路由器覆盖。", config_.uri_string);
            }
        } else {
            if (config_.client_side_routing_enabled && config_.initial_router_addresses_override.empty() && parsed_initial_uri_.is_routing_scheme && !parsed_initial_uri_.hosts_with_ports.empty()) {
                std::vector<routing::ServerAddress> initial_routers_from_uri;
                for (const auto& hp : parsed_initial_uri_.hosts_with_ports) {
                    initial_routers_from_uri.emplace_back(hp.first, hp.second);
                }
                std::string default_context_key = Neo4jBoltTransport::_make_routing_context_key("", std::nullopt);
                config_.initial_router_addresses_override[default_context_key] = initial_routers_from_uri;
                if (config_.logger) config_.logger->info("[TransportLC] 从URI为上下文 '{}' 设置了 {} 个初始路由器。", default_context_key, initial_routers_from_uri.size());
            }
        }

        finalized_user_agent_ = config_.user_agent_override;
        if (finalized_user_agent_.empty() && !config_.bolt_agent_info.product.empty()) {
            finalized_user_agent_ = config_.bolt_agent_info.product;
        }
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
            if (config_.logger) config_.logger->info("[TransportLC] 连接驱逐任务已调度在 {}ms 后。", first_eviction_delay_ms);
        }
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport 初始化完成。");
    }

    Neo4jBoltTransport::~Neo4jBoltTransport() {
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport 正在析构。");
        close();
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport 析构完成。");
    }

    void Neo4jBoltTransport::close() {
        if (closing_.exchange(true)) {
            if (config_.logger) config_.logger->debug("[TransportLC] Close 已被调用或正在进行中。");
            return;
        }
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport 正在关闭...");

        if (eviction_timer_) {
            try {
                // 调用无参数的 cancel() 版本
                std::size_t cancelled_count = eviction_timer_->cancel();
                if (config_.logger) {
                    config_.logger->trace("[TransportLC] Eviction timer cancelled {} pending operations.", cancelled_count);
                }
            } catch (const boost::system::system_error& e) {
                // 如果 cancel() 抛出异常（例如，底层服务错误）
                if (config_.logger) {
                    config_.logger->warn("[TransportLC] 取消驱逐定时器时发生错误: {}", e.what());
                }
            }
            eviction_timer_.reset();  // 释放定时器对象
        }

        // ... (后续的 close() 逻辑保持不变) ...
        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            if (config_.logger) config_.logger->debug("[TransportLC] 正在终止 {} 个空闲连接。", idle_connections_.size());
            for (auto& conn_ptr : idle_connections_) {
                if (conn_ptr) {
                    conn_ptr->terminate(true);
                }
            }
            idle_connections_.clear();
            total_connections_currently_pooled_ = 0;
            total_connections_ever_created_ = 0;
        }
        pool_condition_.notify_all();

        {
            std::lock_guard<std::mutex> lock(routing_table_mutex_);
            routing_tables_.clear();
            if (config_.logger) config_.logger->debug("[TransportLC] 路由表已清除。");
        }

        if (work_guard_) {
            work_guard_->reset();
        }
        if (!io_context_.stopped()) {
            io_context_.stop();
        }

        if (own_io_thread_ && io_thread_ && io_thread_->joinable()) {
            if (config_.logger) config_.logger->debug("[TransportLC] 正在等待 IO 线程完成...");
            try {
                io_thread_->join();
                if (config_.logger) config_.logger->debug("[TransportLC] IO 线程已完成。");
            } catch (const std::system_error& e) {
                if (config_.logger) config_.logger->error("[TransportLC] 等待 IO 线程时发生错误: {}", e.what());
            }
        }
        if (config_.logger) config_.logger->info("[TransportLC] Neo4jBoltTransport 已关闭。");
    }

    // ... (verify_connectivity 和 _create_physical_connection_config 保持之前的修复) ...
    boltprotocol::BoltError Neo4jBoltTransport::verify_connectivity() {
        if (closing_.load(std::memory_order_acquire)) {
            if (config_.logger) config_.logger->warn("[TransportVerify] 尝试在关闭的 transport 上验证连接性。");
            return boltprotocol::BoltError::UNKNOWN_ERROR;
        }

        if (config_.logger) config_.logger->info("[TransportVerify] 正在验证连接性...");

        routing::ServerAddress address_to_verify;
        bool use_routing_for_verify = config_.client_side_routing_enabled && (parsed_initial_uri_.scheme != "bolt" && parsed_initial_uri_.scheme != "bolt+s" && parsed_initial_uri_.scheme != "bolt+ssc");

        if (use_routing_for_verify) {
            auto [addr_err, router_addr] = _get_server_address_for_session(config::SessionParameters{}.with_database("system"), routing::ServerRole::ROUTER);
            if (addr_err != boltprotocol::BoltError::SUCCESS || router_addr.host.empty()) {
                if (config_.logger) config_.logger->warn("[TransportVerify] 验证连接性失败：无法获取路由地址。错误: {}", error::bolt_error_to_string(addr_err));
                if (!parsed_initial_uri_.hosts_with_ports.empty()) {
                    const auto& hp = parsed_initial_uri_.hosts_with_ports.front();
                    address_to_verify = routing::ServerAddress(hp.first, hp.second);
                    if (config_.logger) config_.logger->debug("[TransportVerify] 路由地址获取失败，尝试直接连接到 {}", address_to_verify.to_string());
                } else {
                    if (config_.logger) config_.logger->error("[TransportVerify] 验证连接性失败：无可用路由地址且无直接连接地址。");
                    return boltprotocol::BoltError::NETWORK_ERROR;
                }
            } else {
                address_to_verify = router_addr;
            }
        } else {
            if (parsed_initial_uri_.hosts_with_ports.empty()) {
                if (config_.logger) config_.logger->error("[TransportVerify] 验证连接性失败：无直接连接地址。");
                return boltprotocol::BoltError::INVALID_ARGUMENT;
            }
            const auto& hp = parsed_initial_uri_.hosts_with_ports.front();
            address_to_verify = routing::ServerAddress(hp.first, hp.second);
        }

        if (address_to_verify.host.empty()) {
            if (config_.logger) config_.logger->error("[TransportVerify] 验证连接性失败：最终的验证地址为空。");
            return boltprotocol::BoltError::INVALID_ARGUMENT;
        }

        routing::ServerAddress resolved_address_to_verify = address_to_verify;
        if (config_.server_address_resolver) {
            resolved_address_to_verify = config_.server_address_resolver(address_to_verify);
        }

        if (config_.logger) config_.logger->debug("[TransportVerify] 尝试连接到 {} (原始: {}) 以验证连接性。", resolved_address_to_verify.to_string(), address_to_verify.to_string());

        auto [conn_err, conn] = _acquire_pooled_connection(resolved_address_to_verify, std::nullopt);
        if (conn_err != boltprotocol::BoltError::SUCCESS || !conn) {
            if (config_.logger) config_.logger->error("[TransportVerify] 验证连接性失败：无法获取到 {} 的连接。错误: {}", resolved_address_to_verify.to_string(), error::bolt_error_to_string(conn_err));
            return conn_err;
        }

        if (config_.logger) config_.logger->info("[TransportVerify] 连接性验证成功 (连接到 {})。", resolved_address_to_verify.to_string());
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
        physical_conf.socket_keep_alive_enabled = config_.tcp_keep_alive_enabled;
        physical_conf.tcp_no_delay_enabled = config_.tcp_no_delay_enabled;
        physical_conf.bolt_handshake_timeout_ms = config_.tcp_connect_timeout_ms;

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
            config_.logger->trace("[TransportLC] 创建物理连接配置: Host={}, Port={}, Enc={}, Strategy={}, TCPNoDelay={}, HelloRoutingCtx={}, PreferredBoltVersions=[{}]",
                                  physical_conf.target_host,
                                  physical_conf.target_port,
                                  physical_conf.encryption_enabled,
                                  static_cast<int>(physical_conf.resolved_encryption_strategy),
                                  physical_conf.tcp_no_delay_enabled,
                                  physical_conf.hello_routing_context.has_value() ? "是" : "否",
                                  preferred_versions_str);
        }

        return physical_conf;
    }

}  // namespace neo4j_bolt_transport