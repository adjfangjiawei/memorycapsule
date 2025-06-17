#include <algorithm>  // For std::remove_if
#include <chrono>
#include <iostream>  // 调试用

#include "neo4j_bolt_transport/error/neo4j_error_util.h"  // 虽然这里可能不需要，但保持一致性
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    void Neo4jBoltTransport::_evict_stale_connections_task() {
        if (closing_.load(std::memory_order_acquire)) return;

        if (config_.logger) config_.logger->trace("[PoolEvictor] 开始检查过期的空闲连接...");

        std::unique_lock<std::mutex> lock(pool_mutex_);
        if (closing_.load(std::memory_order_acquire)) return;  // 再次检查，因为获取锁可能耗时

        auto now = std::chrono::steady_clock::now();
        int evicted_count = 0;

        auto it_remove = std::remove_if(idle_connections_.begin(), idle_connections_.end(), [&](const internal::BoltPhysicalConnection::PooledConnection& conn_ptr) {
            if (!conn_ptr) return true;  // 防御性编程

            bool evict = false;
            std::string reason;
            routing::ServerAddress conn_target(conn_ptr->get_config().target_host, conn_ptr->get_config().target_port);

            if (config_.max_connection_lifetime_ms > 0) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - conn_ptr->get_creation_timestamp()).count() > static_cast<long long>(config_.max_connection_lifetime_ms)) {
                    evict = true;
                    reason = "达到最大生命周期";
                }
            }
            if (!evict && config_.idle_timeout_ms > 0) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - conn_ptr->get_last_used_timestamp()).count() > static_cast<long long>(config_.idle_timeout_ms)) {
                    evict = true;
                    reason = "空闲超时";
                }
            }
            if (!evict && conn_ptr->is_defunct()) {  // 安全检查，理论上不应在池中
                evict = true;
                reason = "在池中发现失效连接";
            }

            if (evict) {
                if (config_.logger) config_.logger->info("[PoolEvictor] 驱逐连接 {} (到 {}) 原因: {}.", conn_ptr->get_id(), conn_target.to_string(), reason);
                conn_ptr->terminate(false);  // 终止，不发送 GOODBYE
                return true;                 // 标记为移除
            }
            return false;  // 保留
        });

        evicted_count = std::distance(it_remove, idle_connections_.end());
        if (evicted_count > 0) {
            idle_connections_.erase(it_remove, idle_connections_.end());
            total_connections_currently_pooled_ -= evicted_count;
            total_connections_ever_created_ -= evicted_count;
            if (config_.logger) config_.logger->debug("[PoolEvictor] 驱逐了 {} 个连接。当前空闲: {}. 总创建数: {}", evicted_count, total_connections_currently_pooled_, total_connections_ever_created_);
            pool_condition_.notify_all();  // 通知可能因池满而等待的线程
        }
        lock.unlock();  // 手动解锁

        // 重新调度驱逐任务 (如果 transport 未关闭)
        if (!closing_.load(std::memory_order_acquire) && eviction_timer_ && (config_.idle_timeout_ms > 0 || config_.max_connection_lifetime_ms > 0)) {
            // 计算下一个合理的检查时间，例如最短超时的一半，但不小于1秒
            uint32_t next_check_interval_ms = 10000;  // 默认10秒
            if (config_.idle_timeout_ms > 0 && config_.max_connection_lifetime_ms > 0) {
                next_check_interval_ms = std::min(config_.idle_timeout_ms, config_.max_connection_lifetime_ms);
            } else if (config_.idle_timeout_ms > 0) {
                next_check_interval_ms = config_.idle_timeout_ms;
            } else if (config_.max_connection_lifetime_ms > 0) {
                next_check_interval_ms = config_.max_connection_lifetime_ms;
            }
            next_check_interval_ms = std::max(1000u, next_check_interval_ms / 2);

            eviction_timer_->expires_after(std::chrono::milliseconds(next_check_interval_ms));
            eviction_timer_->async_wait([this](const boost::system::error_code& ec) {
                if (ec != boost::asio::error::operation_aborted && !closing_.load(std::memory_order_relaxed)) {
                    _evict_stale_connections_task();
                }
            });
            if (config_.logger) config_.logger->trace("[PoolEvictor] 下一次连接驱逐检查已调度在 {}ms 后。", next_check_interval_ms);
        } else if (config_.logger) {
            config_.logger->trace("[PoolEvictor] 连接驱逐任务未重新调度 (transport关闭或定时器/配置禁用)。");
        }
    }

}  // namespace neo4j_bolt_transport