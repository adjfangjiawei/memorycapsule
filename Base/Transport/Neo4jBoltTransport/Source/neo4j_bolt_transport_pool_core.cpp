#include <algorithm>
#include <chrono>
#include <iostream>  // 调试用

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // _acquire_pooled_connection: 从池中获取或创建一个到 *特定已知地址* 的连接。
    // 路由选择逻辑在此函数之前完成。
    std::pair<boltprotocol::BoltError, internal::BoltPhysicalConnection::PooledConnection> Neo4jBoltTransport::_acquire_pooled_connection(const routing::ServerAddress& target_address, const std::optional<std::string>& database_name_hint /*用于日志和未来可能的优化*/) {
        if (closing_.load(std::memory_order_acquire)) {
            if (config_.logger) config_.logger->warn("[PoolCore] 尝试在关闭的 transport 上获取到 {} 的连接。", target_address.to_string());
            return {boltprotocol::BoltError::UNKNOWN_ERROR, nullptr};
        }

        std::unique_lock<std::mutex> lock(pool_mutex_);
        auto start_time = std::chrono::steady_clock::now();

        while (true) {
            // 1. 尝试从空闲连接中查找一个到 target_address 的可用连接
            for (auto it = idle_connections_.begin(); it != idle_connections_.end(); /* manual increment */) {
                internal::BoltPhysicalConnection::PooledConnection& conn_ptr_ref = *it;

                if (conn_ptr_ref->get_config().target_host == target_address.host && conn_ptr_ref->get_config().target_port == target_address.port) {
                    internal::BoltPhysicalConnection::PooledConnection conn_to_check = std::move(conn_ptr_ref);
                    it = idle_connections_.erase(it);  // 从池中移除
                    total_connections_currently_pooled_--;

                    bool healthy = true;
                    std::string unhealthy_reason;

                    if (conn_to_check->is_defunct()) {
                        healthy = false;
                        unhealthy_reason = "is_defunct";
                    } else if (config_.max_connection_lifetime_ms > 0 && (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - conn_to_check->get_creation_timestamp()).count() > static_cast<long long>(config_.max_connection_lifetime_ms))) {
                        healthy = false;
                        unhealthy_reason = "exceeded max lifetime";
                    } else if (config_.idle_time_before_health_check_ms > 0 && (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - conn_to_check->get_last_used_timestamp()).count() > static_cast<long long>(config_.idle_time_before_health_check_ms))) {
                        if (config_.logger) config_.logger->trace("[PoolCore] 空闲连接 {} (到 {}) 需要健康检查 (ping)。", conn_to_check->get_id(), target_address.to_string());

                        lock.unlock();
                        boltprotocol::BoltError ping_err = conn_to_check->ping(std::chrono::milliseconds(config_.tcp_connect_timeout_ms));
                        lock.lock();

                        if (closing_.load(std::memory_order_acquire)) {
                            if (config_.logger) config_.logger->warn("[PoolCore] Ping 后 transport 关闭。");
                            if (conn_to_check) conn_to_check->terminate(false);
                            total_connections_ever_created_--;
                            return {boltprotocol::BoltError::UNKNOWN_ERROR, nullptr};
                        }

                        if (ping_err != boltprotocol::BoltError::SUCCESS) {
                            healthy = false;
                            unhealthy_reason = "ping failed (" + error::bolt_error_to_string(ping_err) + ")";
                        } else {
                            if (config_.logger) config_.logger->trace("[PoolCore] 空闲连接 {} (到 {}) ping 成功。", conn_to_check->get_id(), target_address.to_string());
                        }
                    }

                    if (healthy) {
                        if (config_.logger) config_.logger->debug("[PoolCore] 复用空闲连接 {} 到 {} (数据库提示: '{}')", conn_to_check->get_id(), target_address.to_string(), database_name_hint.value_or("<无>"));
                        conn_to_check->mark_as_used();
                        return {boltprotocol::BoltError::SUCCESS, std::move(conn_to_check)};
                    } else {
                        if (config_.logger) config_.logger->info("[PoolCore] 终止过时/不健康的空闲连接 {} (到 {}) (原因: {}).", (conn_to_check ? conn_to_check->get_id() : 0), target_address.to_string(), unhealthy_reason);
                        if (conn_to_check) conn_to_check->terminate(false);
                        total_connections_ever_created_--;
                        // 继续在 idle_connections_ 中查找 (迭代器已通过 erase 更新)
                        // it = idle_connections_.erase(it) 已经移动了迭代器，所以不需要 it++
                        continue;
                    }
                }
                ++it;  // 检查下一个空闲连接
            }

            // 2. 如果池中没有到 target_address 的合适连接，并且池未满，则创建新连接
            if (total_connections_ever_created_ < config_.max_connection_pool_size) {
                std::optional<std::map<std::string, boltprotocol::Value>> hello_routing_ctx_opt;
                if (config_.client_side_routing_enabled) {
                    std::map<std::string, boltprotocol::Value> ctx_map;
                    ctx_map["address"] = target_address.to_string();  // HELLO 上下文是目标地址
                    hello_routing_ctx_opt = ctx_map;
                }

                internal::BoltConnectionConfig physical_conn_conf = _create_physical_connection_config(target_address, hello_routing_ctx_opt);
                std::shared_ptr<spdlog::logger> conn_logger = config_.get_or_create_logger("BoltConnection");

                lock.unlock();
                if (config_.logger) config_.logger->debug("[PoolCore] 创建到 {} 的新连接 (数据库提示: '{}')", target_address.to_string(), database_name_hint.value_or("<无>"));

                auto new_conn = std::make_unique<internal::BoltPhysicalConnection>(std::move(physical_conn_conf), io_context_, conn_logger);
                boltprotocol::BoltError establish_err = new_conn->establish();
                lock.lock();

                if (closing_.load(std::memory_order_acquire)) {
                    if (config_.logger) config_.logger->warn("[PoolCore] Transport 在新连接建立期间关闭。");
                    if (new_conn) new_conn->terminate(false);
                    return {boltprotocol::BoltError::UNKNOWN_ERROR, nullptr};
                }

                if (establish_err == boltprotocol::BoltError::SUCCESS) {
                    if (config_.logger) config_.logger->info("[PoolCore] 到 {} 的新连接 {} 已建立。", target_address.to_string(), new_conn->get_id());
                    total_connections_ever_created_++;
                    new_conn->mark_as_used();
                    return {boltprotocol::BoltError::SUCCESS, std::move(new_conn)};
                } else {
                    if (config_.logger) config_.logger->error("[PoolCore] 无法建立到 {} 的新连接。错误: {} ({})", target_address.to_string(), static_cast<int>(establish_err), new_conn ? new_conn->get_last_error_message() : error::bolt_error_to_string(establish_err));
                    // 连接失败不应该直接在这里处理路由表，因为此函数只负责连接到 *给定* 地址。
                    // 调用此函数的上层（例如路由逻辑）应该处理地址不可达的情况。
                    return {establish_err, nullptr};
                }
            }

            // 3. 如果池已满，等待
            auto time_waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
            auto remaining_timeout_ms = std::chrono::milliseconds(config_.connection_acquisition_timeout_ms) - time_waited;

            if (remaining_timeout_ms <= std::chrono::milliseconds(0)) {
                if (config_.logger) config_.logger->error("[PoolCore] 等待到 {} 的连接超时 (最大池大小: {})。", target_address.to_string(), config_.max_connection_pool_size);
                return {boltprotocol::BoltError::UNKNOWN_ERROR, nullptr};  // TODO: 更具体的超时错误码
            }

            if (config_.logger) {
                config_.logger->trace("[PoolCore] 池已满 ({}/{})，等待 {}ms 获取到 {} 的连接。",
                                      total_connections_ever_created_,
                                      config_.max_connection_pool_size,  // 使用 ever_created 作为当前“正在使用或空闲”的总数上限
                                      remaining_timeout_ms.count(),
                                      target_address.to_string());
            }

            if (pool_condition_.wait_for(lock, remaining_timeout_ms, [this] {
                    return closing_.load(std::memory_order_relaxed) || !idle_connections_.empty();  // 等待有任何空闲连接或关闭
                })) {
                if (closing_.load(std::memory_order_acquire)) {
                    if (config_.logger) config_.logger->warn("[PoolCore] 等待期间 transport 关闭。");
                    return {boltprotocol::BoltError::UNKNOWN_ERROR, nullptr};
                }
                if (config_.logger) config_.logger->trace("[PoolCore] 被唤醒，可能有空闲连接或 transport 关闭。");
            } else {
                if (config_.logger) config_.logger->error("[PoolCore] 等待到 {} 的连接在 wait_for 后超时 (最大池大小: {})。", target_address.to_string(), config_.max_connection_pool_size);
                return {boltprotocol::BoltError::UNKNOWN_ERROR, nullptr};  // 超时
            }
            // 继续循环
        }
    }

    void Neo4jBoltTransport::release_connection(internal::BoltPhysicalConnection::PooledConnection connection, bool mark_as_healthy) {
        if (!connection) return;

        bool transport_is_closing = closing_.load(std::memory_order_acquire);
        uint64_t conn_id = connection->get_id();
        routing::ServerAddress conn_target(connection->get_config().target_host, connection->get_config().target_port);

        if (transport_is_closing) {
            if (config_.logger) config_.logger->debug("[PoolCore] 在 transport 关闭期间释放连接 {} (到 {}), 将其终止。", conn_id, conn_target.to_string());
            connection->terminate(false);  // 不发送 GOODBYE
            // total_connections_ever_created_ 应该在这里减少，因为这个连接不再存在于系统中。
            // 加锁是为了保护 total_connections_ever_created_
            std::lock_guard<std::mutex> lock(pool_mutex_);
            total_connections_ever_created_ = std::max(0, (int)total_connections_ever_created_ - 1);  // 确保不为负
            return;
        }

        std::lock_guard<std::mutex> lock(pool_mutex_);

        if (!mark_as_healthy || connection->is_defunct()) {
            if (config_.logger) config_.logger->info("[PoolCore] 释放不健康/失效的连接 {} (到 {}), 将其终止。健康标记: {}, 失效: {}", conn_id, conn_target.to_string(), mark_as_healthy, connection->is_defunct());
            connection->terminate(false);
            total_connections_ever_created_--;
            pool_condition_.notify_one();
            return;
        }

        bool should_retire_due_to_age = false;
        if (config_.max_connection_lifetime_ms > 0) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - connection->get_creation_timestamp());
            if (age.count() > static_cast<long long>(config_.max_connection_lifetime_ms)) {
                should_retire_due_to_age = true;
                if (config_.logger) config_.logger->info("[PoolCore] 连接 {} (到 {}) 因达到最大生命周期而淘汰。", conn_id, conn_target.to_string());
            }
        }

        // 如果空闲连接数已达到池上限（max_connection_pool_size 通常指总连接数，但这里也可理解为空闲连接上限）
        // 或者连接已老化，则关闭此连接
        // 注意：max_connection_pool_size 应该与 total_connections_ever_created_ 比较。
        // total_connections_currently_pooled_ 是当前空闲的连接数。
        if (should_retire_due_to_age || total_connections_currently_pooled_ >= config_.max_connection_pool_size) {
            if (config_.logger) config_.logger->debug("[PoolCore] 终止连接 {} (到 {})。淘汰: {}, 当前空闲: {}, 配置池大小(上限): {}", conn_id, conn_target.to_string(), should_retire_due_to_age, total_connections_currently_pooled_, config_.max_connection_pool_size);
            connection->terminate(true);  // 发送 GOODBYE
            total_connections_ever_created_--;
            pool_condition_.notify_one();
        } else {
            if (config_.logger) config_.logger->debug("[PoolCore] 将连接 {} (到 {}) 返回到空闲池。当前空闲池大小: {}", conn_id, conn_target.to_string(), total_connections_currently_pooled_);
            connection->mark_as_used();  // 更新最后使用时间戳
            idle_connections_.push_back(std::move(connection));
            total_connections_currently_pooled_++;
            pool_condition_.notify_one();
        }
    }

}  // namespace neo4j_bolt_transport