#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // _get_server_address_for_session: 获取会话的服务器地址，可能会使用路由
    std::pair<boltprotocol::BoltError, routing::ServerAddress> Neo4jBoltTransport::_get_server_address_for_session(const config::SessionParameters& params, routing::ServerRole role_hint) {
        if (config_.logger) {
            config_.logger->trace(
                "[AddrSelect] 获取服务器地址, 数据库: '{}', 角色提示: {}, 模拟用户: '{}'", params.database_name.value_or("<默认>"), (role_hint == routing::ServerRole::READER ? "READER" : (role_hint == routing::ServerRole::WRITER ? "WRITER" : "ROUTER")), params.impersonated_user.value_or("<无>"));
        }

        // 如果未启用客户端路由，或者使用的是直接的 bolt:// 方案
        if (!config_.client_side_routing_enabled || parsed_initial_uri_.scheme == "bolt" || parsed_initial_uri_.scheme == "bolt+s" || parsed_initial_uri_.scheme == "bolt+ssc") {
            if (parsed_initial_uri_.hosts_with_ports.empty()) {
                if (config_.logger) config_.logger->error("[AddrSelect] 无可用主机用于直接连接。");
                return {boltprotocol::BoltError::INVALID_ARGUMENT, {}};
            }
            // 对于直接连接，使用URI中的第一个主机
            const auto& host_port = parsed_initial_uri_.hosts_with_ports.front();
            routing::ServerAddress resolved_address(host_port.first, host_port.second);

            // 应用自定义地址解析器（如果提供）
            if (config_.server_address_resolver) {
                routing::ServerAddress original_address = resolved_address;
                resolved_address = config_.server_address_resolver(original_address);
                if (config_.logger && (original_address.host != resolved_address.host || original_address.port != resolved_address.port)) {
                    config_.logger->debug("[AddrSelect] 直接连接地址已解析: {} -> {}", original_address.to_string(), resolved_address.to_string());
                }
            }
            if (config_.logger) config_.logger->debug("[AddrSelect] 直接连接，使用地址: {}", resolved_address.to_string());
            return {boltprotocol::BoltError::SUCCESS, resolved_address};
        }

        // --- 需要路由 ---
        // 确定用于路由的数据库名称。对于 neo4j:// 方案，空数据库名通常指默认集群/数据库，
        // 或者需要先连接到 system 数据库获取集群信息。
        // 驱动通常会为每个 (database_name, impersonated_user) 组合维护一个路由表。
        std::string db_name_for_routing_key = params.database_name.value_or("");

        // 尝试获取或刷新路由表
        std::shared_ptr<routing::RoutingTable> routing_table = _get_or_fetch_routing_table(db_name_for_routing_key, params.impersonated_user);

        if (!routing_table) {
            if (config_.logger) config_.logger->error("[AddrSelect] 无法获取或刷新数据库 '{}' 的路由表 (模拟用户: '{}')", db_name_for_routing_key, params.impersonated_user.value_or("<无>"));
            return {boltprotocol::BoltError::NETWORK_ERROR, {}};  // 或者更具体的路由错误
        }

        // 从路由表中选择一个服务器
        // 尝试多次，因为表可能在两次调用之间变得陈旧，或者选中的服务器刚好失效
        int attempts = 0;
        const int max_selection_attempts = config_.routing_max_retry_attempts > 0 ? config_.routing_max_retry_attempts : 3;  // 至少尝试1次

        while (attempts < max_selection_attempts) {
            attempts++;
            if (routing_table->is_stale() && attempts > 1) {  // 如果不是第一次尝试且表已过时
                if (config_.logger) config_.logger->info("[AddrSelect] 路由表 '{}' 在选择期间已过时，第 {} 次尝试刷新。", routing_table->get_database_context_key(), attempts);
                // 重新获取/刷新路由表
                routing_table = _get_or_fetch_routing_table(db_name_for_routing_key, params.impersonated_user);
                if (!routing_table) {
                    if (config_.logger) config_.logger->error("[AddrSelect] 路由表 '{}' 刷新失败。", db_name_for_routing_key);
                    return {boltprotocol::BoltError::NETWORK_ERROR, {}};
                }
            }

            std::optional<routing::ServerAddress> server_address_opt = routing_table->get_server(role_hint);

            if (server_address_opt) {
                routing::ServerAddress resolved_address = *server_address_opt;
                // 应用自定义地址解析器
                if (config_.server_address_resolver) {
                    routing::ServerAddress original_address = resolved_address;
                    resolved_address = config_.server_address_resolver(original_address);
                    if (config_.logger && (original_address.host != resolved_address.host || original_address.port != resolved_address.port)) {
                        config_.logger->debug("[AddrSelect] 路由选定地址已解析: {} -> {}", original_address.to_string(), resolved_address.to_string());
                    }
                }
                if (config_.logger) config_.logger->info("[AddrSelect] 选定服务器地址: {} (角色: {}), 尝试次数: {}", resolved_address.to_string(), (role_hint == routing::ServerRole::READER ? "READER" : (role_hint == routing::ServerRole::WRITER ? "WRITER" : "ROUTER")), attempts);
                return {boltprotocol::BoltError::SUCCESS, resolved_address};
            } else {
                if (config_.logger) config_.logger->warn("[AddrSelect] 第 {} 次尝试: 路由表 '{}' 中没有找到角色 {} 的可用服务器。", attempts, routing_table->get_database_context_key(), static_cast<int>(role_hint));
                if (attempts < max_selection_attempts) {
                    routing_table->mark_as_stale();  // 强制下次迭代时刷新
                                                     // 可以选择在这里短暂 sleep，或者让上层（如连接获取）处理重试
                }
            }
        }  // end while attempts

        if (config_.logger) config_.logger->error("[AddrSelect] 多次尝试后，路由表 '{}' 中仍无法为角色 {} 找到服务器。", routing_table->get_database_context_key(), static_cast<int>(role_hint));
        return {boltprotocol::BoltError::NETWORK_ERROR, {}};  // 或者 "No suitable server found"
    }

}  // namespace neo4j_bolt_transport