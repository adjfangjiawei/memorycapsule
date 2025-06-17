#include <chrono>  // For std::chrono::seconds default TTL

#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // 可能需要用于日志
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // 静态辅助函数，已在 pool_core.cpp 中定义，这里为保持独立性可重新声明或包含一个通用头文件
    // static std::string make_routing_context_key(const std::string& database_name, const std::optional<std::string>& impersonated_user) {
    //     std::string db_part = database_name.empty() ? "system" : database_name;
    //     if (impersonated_user && !impersonated_user->empty()) {
    //         return db_part + "@" + *impersonated_user;
    //     }
    //     return db_part;
    // }
    // ^^^ 如果这个函数在多个 .cpp 文件中需要，最好放到一个共享的辅助头文件中，或者作为 Neo4jBoltTransport 的私有静态成员。
    // 为简单起见，暂时假设它在 neo4j_bolt_transport_pool_core.cpp 中定义的版本可以被链接器找到，
    // 或者直接在 Neo4jBoltTransport 类中定义它。
    // 这里我们直接在类作用域外（或在一个辅助命名空间）定义它，假设它是一个全局辅助函数。
    // (在实际项目中，会放到一个 util.h 或类似文件中)
    namespace detail {  // 使用一个内部命名空间避免冲突
        std::string make_routing_context_key_for_manager(const std::string& database_name, const std::optional<std::string>& impersonated_user) {
            std::string db_part = database_name.empty() ? "system" : database_name;
            if (impersonated_user && !impersonated_user->empty()) {
                return db_part + "@" + *impersonated_user;
            }
            return db_part;
        }
    }  // namespace detail

    // 获取或创建（如果不存在）并可能刷新路由表
    std::shared_ptr<routing::RoutingTable> Neo4jBoltTransport::_get_or_fetch_routing_table(const std::string& database_name, const std::optional<std::string>& impersonated_user) {
        std::string context_key = detail::make_routing_context_key_for_manager(database_name, impersonated_user);
        std::shared_ptr<routing::RoutingTable> table;
        std::vector<routing::ServerAddress> initial_routers_for_this_context;  // 用于刷新

        {  // 作用域锁保护 routing_tables_ 的访问
            std::lock_guard<std::mutex> lock(routing_table_mutex_);
            auto it = routing_tables_.find(context_key);
            if (it != routing_tables_.end()) {
                table = it->second;
            } else {
                // 从配置中获取此上下文的默认 TTL (例如，300秒)
                // 注意: config_.routing_table_default_ttl_seconds (如果添加了这个配置项)
                unsigned int default_ttl_seconds = 300;
                // if (config_.routing_table_default_ttl_seconds.has_value()) {
                //    default_ttl_seconds = *config_.routing_table_default_ttl_seconds;
                // }
                table = std::make_shared<routing::RoutingTable>(context_key, std::chrono::seconds(default_ttl_seconds));
                routing_tables_[context_key] = table;
                if (config_.logger) config_.logger->info("[RoutingMgr] 为上下文 '{}' 创建了新的路由表实例 (默认TTL: {}s)。", context_key, default_ttl_seconds);
            }
        }  // 解锁 routing_table_mutex_

        // 确定用于获取此上下文路由表的初始路由器
        // 优先级：1. 配置覆盖 specific_context_key 2. 配置覆盖 "default" 或 "" key 3. 从主URI解析
        bool initial_routers_found = false;
        if (config_.initial_router_addresses_override.count(context_key)) {
            initial_routers_for_this_context = config_.initial_router_addresses_override.at(context_key);
            if (!initial_routers_for_this_context.empty()) initial_routers_found = true;
            if (config_.logger && initial_routers_found) config_.logger->trace("[RoutingMgr] 上下文 '{}' 使用了配置中覆盖的初始路由器。", context_key);
        }

        if (!initial_routers_found) {
            // 尝试通用的初始路由器配置 (例如，用户可能只配置了一组全局初始路由器)
            std::string generic_initial_router_key = "";  // 或者一个特殊的配置键
            if (config_.initial_router_addresses_override.count(generic_initial_router_key)) {
                initial_routers_for_this_context = config_.initial_router_addresses_override.at(generic_initial_router_key);
                if (!initial_routers_for_this_context.empty()) initial_routers_found = true;
                if (config_.logger && initial_routers_found) config_.logger->trace("[RoutingMgr] 上下文 '{}' 使用了通用的初始路由器配置。", context_key);
            }
        }

        if (!initial_routers_found && !parsed_initial_uri_.hosts_with_ports.empty() && parsed_initial_uri_.is_routing_scheme) {
            for (const auto& hp : parsed_initial_uri_.hosts_with_ports) {
                initial_routers_for_this_context.emplace_back(hp.first, hp.second);
            }
            if (!initial_routers_for_this_context.empty()) initial_routers_found = true;
            if (config_.logger && initial_routers_found) config_.logger->trace("[RoutingMgr] 上下文 '{}' 使用了从主URI解析的初始路由器。", context_key);
        }

        if (!initial_routers_found) {
            if (config_.logger) config_.logger->error("[RoutingMgr] 无法确定用于刷新上下文 '{}' 的初始路由器。", context_key);
            // table->mark_as_stale(); // 确保它被标记为过时
            return nullptr;  // 无法刷新，返回空指针或当前的（可能是过时的）表
        }

        // 如果表已过期，则尝试刷新它
        // 使用一个更细粒度的锁或原子标志来避免在刷新时阻塞其他对此表的请求可能更好，但目前简化处理
        if (table->is_stale()) {
            if (config_.logger) config_.logger->info("[RoutingMgr] 路由表 '{}' 已过期或从未更新，尝试刷新。", context_key);

            // _fetch_and_update_routing_table 内部会连接到路由器并发送 ROUTE 消息
            boltprotocol::BoltError refresh_err = _fetch_and_update_routing_table(table, initial_routers_for_this_context, database_name, impersonated_user);

            if (refresh_err != boltprotocol::BoltError::SUCCESS) {
                if (config_.logger) config_.logger->error("[RoutingMgr] 刷新路由表 '{}' 失败，错误: {}", context_key, static_cast<int>(refresh_err));
                // 保留旧表（可能是空的或过期的），调用者需要处理
                // 或者，如果刷新失败意味着我们无法信任当前表，则返回nullptr
                return nullptr;  // 表示刷新失败
            }
            if (config_.logger) config_.logger->info("[RoutingMgr] 路由表 '{}' 刷新成功。", context_key);
        } else {
            if (config_.logger) config_.logger->trace("[RoutingMgr] 路由表 '{}' 仍然有效，无需刷新。", context_key);
        }
        return table;
    }

}  // namespace neo4j_bolt_transport