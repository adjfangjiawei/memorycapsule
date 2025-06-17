#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // 可能用于日志
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // 当连接到某个服务器失败时，或服务器返回特定的可路由错误时，调用此函数
    void Neo4jBoltTransport::_handle_routing_failure(const routing::ServerAddress& failed_address,
                                                     const std::string& database_context_key) {  // database_context_key 用于定位正确的路由表

        if (!config_.client_side_routing_enabled) {
            return;  // 如果路由未启用，则不执行任何操作
        }

        if (config_.logger) {
            config_.logger->info("[RoutingFail] 处理路由失败: 地址 {}, 上下文键 {}", failed_address.to_string(), database_context_key);
        }

        std::lock_guard<std::mutex> lock(routing_table_mutex_);
        auto it = routing_tables_.find(database_context_key);
        if (it != routing_tables_.end()) {
            std::shared_ptr<routing::RoutingTable> table = it->second;
            if (table) {
                table->forget_server(failed_address);  // 从路由表中移除失败的服务器
                // 忘记服务器后，路由表可能会变得不健康（例如，没有可用的writer了）
                // RoutingTable::forget_server 内部可能会调用 mark_as_stale()
                // 如果需要更主动的刷新，可以在这里调用 table->mark_as_stale();
                if (config_.logger) {
                    config_.logger->debug("[RoutingFail] 从路由表 '{}' 中移除了地址 {}。", database_context_key, failed_address.to_string());
                    if (table->is_stale()) {
                        config_.logger->info("[RoutingFail] 路由表 '{}' 在移除地址后被标记为过时。", database_context_key);
                    }
                }
            }
        } else {
            if (config_.logger) {
                config_.logger->warn("[RoutingFail] 未找到上下文键为 '{}' 的路由表来处理失败。", database_context_key);
            }
        }
    }

}  // namespace neo4j_bolt_transport