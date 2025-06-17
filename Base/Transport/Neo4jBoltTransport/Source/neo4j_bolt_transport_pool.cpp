// Source/neo4j_bolt_transport_pool.cpp
#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    boltprotocol::BoltError Neo4jBoltTransport::acquire_connection(internal::BoltPhysicalConnection::PooledConnection& out_connection, const std::string& for_database) {
        if (closing_.load(std::memory_order_acquire)) {  // Line 14
            if (config_.logger) config_.logger->warn("[Pool] Acquire attempt on closing transport.");
            return boltprotocol::BoltError::UNKNOWN_ERROR;
        }

        std::unique_lock<std::mutex> lock(pool_mutex_);
        auto start_time = std::chrono::steady_clock::now();

        while (true) {
            while (!idle_connections_.empty()) {
                internal::BoltPhysicalConnection::PooledConnection conn = std::move(idle_connections_.front());
                idle_connections_.pop_front();

                bool healthy = true;
                std::string unhealthy_reason;

                if (conn->is_defunct()) {
                    healthy = false;
                    unhealthy_reason = "is_defunct";
                } else if (config_.max_connection_lifetime_ms > 0 && (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - conn->get_creation_timestamp()).count() > static_cast<long long>(config_.max_connection_lifetime_ms))) {
                    healthy = false;
                    unhealthy_reason = "exceeded max lifetime";
                } else if (config_.idle_time_before_health_check_ms > 0 && (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - conn->get_last_used_timestamp()).count() > static_cast<long long>(config_.idle_time_before_health_check_ms))) {
                    if (config_.logger) config_.logger->trace("[Pool] Idle Conn {} requires health check (ping).", conn->get_id());
                    lock.unlock();
                    boltprotocol::BoltError ping_err = conn->ping(std::chrono::milliseconds(config_.tcp_connect_timeout_ms));
                    lock.lock();

                    if (ping_err != boltprotocol::BoltError::SUCCESS) {
                        healthy = false;
                        unhealthy_reason = "ping failed (" + error::bolt_error_to_string(ping_err) + ")";
                    } else {
                        if (config_.logger) config_.logger->trace("[Pool] Idle Conn {} ping successful.", conn->get_id());
                    }
                }

                if (healthy) {
                    if (config_.logger) config_.logger->debug("[Pool] Reusing idle connection {} for db: '{}'", conn->get_id(), for_database.empty() ? "<default>" : for_database);
                    out_connection = std::move(conn);
                    out_connection->mark_as_used();
                    return boltprotocol::BoltError::SUCCESS;
                } else {
                    if (config_.logger) config_.logger->info("[Pool] Terminating stale/unhealthy idle connection {} (Reason: {}).", (conn ? conn->get_id() : 0), unhealthy_reason);
                    if (conn) conn->terminate(false);
                    total_connections_created_--;
                }
            }

            if (total_connections_created_ < config_.max_connection_pool_size) {
                if (parsed_initial_uri_.hosts_with_ports.empty()) {
                    if (config_.logger) config_.logger->error("[Pool] No hosts available from initial URI to create new connection.");
                    return boltprotocol::BoltError::INVALID_ARGUMENT;
                }
                std::string target_host = parsed_initial_uri_.hosts_with_ports.front().first;
                uint16_t target_port = parsed_initial_uri_.hosts_with_ports.front().second;

                internal::BoltConnectionConfig conn_conf = _create_physical_connection_config(target_host, target_port);
                std::shared_ptr<spdlog::logger> conn_logger = config_.get_or_create_logger();

                lock.unlock();
                if (config_.logger) config_.logger->debug("[Pool] Creating new connection to {}:{} for db: '{}'", target_host, target_port, for_database.empty() ? "<default>" : for_database);

                auto new_conn = std::make_unique<internal::BoltPhysicalConnection>(std::move(conn_conf), io_context_, conn_logger);
                boltprotocol::BoltError establish_err = new_conn->establish();
                lock.lock();

                if (closing_.load(std::memory_order_acquire)) {  // Line 79 - This one looks OK
                    if (new_conn) new_conn->terminate(false);
                    if (config_.logger) config_.logger->warn("[Pool] Transport closing during new connection establishment.");
                    return boltprotocol::BoltError::UNKNOWN_ERROR;
                }

                if (establish_err == boltprotocol::BoltError::SUCCESS) {
                    if (config_.logger) config_.logger->info("[Pool] New connection {} established.", new_conn->get_id());
                    total_connections_created_++;
                    out_connection = std::move(new_conn);
                    out_connection->mark_as_used();
                    return boltprotocol::BoltError::SUCCESS;
                } else {
                    if (config_.logger) config_.logger->error("[Pool] Failed to establish new connection to {}:{}. Error: {} ({})", target_host, target_port, static_cast<int>(establish_err), new_conn ? new_conn->get_last_error_message() : error::bolt_error_to_string(establish_err));
                    return establish_err;
                }
            }

            auto time_waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
            auto remaining_timeout_ms = std::chrono::milliseconds(config_.connection_acquisition_timeout_ms) - time_waited;

            if (remaining_timeout_ms <= std::chrono::milliseconds(0)) {
                if (config_.logger) config_.logger->error("[Pool] Timed out waiting for a connection (Max pool size: {}).", config_.max_connection_pool_size);
                return boltprotocol::BoltError::UNKNOWN_ERROR;
            }

            if (config_.logger) {
                config_.logger->trace("[Pool] Pool full ({}/{}), waiting for {}ms.", total_connections_created_, config_.max_connection_pool_size, remaining_timeout_ms.count());
            }

            if (pool_condition_.wait_for(lock, remaining_timeout_ms, [this] {
                    // Line 115: REMOVE THE SEMICOLON HERE
                    return closing_.load(std::memory_order_relaxed) || !idle_connections_.empty();
                })) {
                if (closing_.load(std::memory_order_acquire)) {  // Line 113 - This one looks OK if Line 115 is fixed
                    if (config_.logger) config_.logger->warn("[Pool] Woken up by closing transport during wait.");
                    return boltprotocol::BoltError::UNKNOWN_ERROR;
                }
                if (config_.logger) config_.logger->trace("[Pool] Woken up, idle connections available or closing.");
            } else {
                if (config_.logger) config_.logger->error("[Pool] Timed out waiting for a connection after wait_for (Max pool size: {}).", config_.max_connection_pool_size);
                return boltprotocol::BoltError::UNKNOWN_ERROR;
            }
        }
    }

    void Neo4jBoltTransport::release_connection(internal::BoltPhysicalConnection::PooledConnection connection, bool mark_as_healthy) {
        if (!connection) return;

        bool transport_is_closing = closing_.load(std::memory_order_acquire);  // Line 130 - This one looks OK
        uint64_t conn_id = connection->get_id();

        if (transport_is_closing) {
            if (config_.logger) config_.logger->debug("[Pool] Releasing conn {} during transport close, terminating.", conn_id);
            connection->terminate(false);
            return;
        }

        std::lock_guard<std::mutex> lock(pool_mutex_);

        if (!mark_as_healthy || connection->is_defunct()) {
            if (config_.logger) config_.logger->info("[Pool] Releasing unhealthy/defunct conn {}, terminating. Healthy: {}, Defunct: {}", conn_id, mark_as_healthy, connection->is_defunct());
            connection->terminate(false);
            total_connections_created_--;
            pool_condition_.notify_one();
            return;
        }

        bool should_retire_due_to_age = false;
        if (config_.max_connection_lifetime_ms > 0) {
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - connection->get_creation_timestamp());
            if (age.count() > static_cast<long long>(config_.max_connection_lifetime_ms)) {
                should_retire_due_to_age = true;
                if (config_.logger) config_.logger->info("[Pool] Retiring conn {} due to max lifetime.", conn_id);
            }
        }

        if (should_retire_due_to_age || idle_connections_.size() >= config_.max_connection_pool_size) {
            if (config_.logger) config_.logger->debug("[Pool] Terminating conn {} on release. Retire: {}, Idle: {}, Max: {}", conn_id, should_retire_due_to_age, idle_connections_.size(), config_.max_connection_pool_size);
            connection->terminate(true);
            total_connections_created_--;
            pool_condition_.notify_one();
        } else {
            if (config_.logger) config_.logger->debug("[Pool] Returning conn {} to idle pool. Idle size: {}", conn_id, idle_connections_.size());
            connection->mark_as_used();
            idle_connections_.push_back(std::move(connection));
            pool_condition_.notify_one();
        }
    }

    void Neo4jBoltTransport::_evict_stale_connections() {
        if (closing_.load(std::memory_order_acquire)) return;  // Line 172 - This one looks OK

        std::lock_guard<std::mutex> lock(pool_mutex_);
        if (closing_.load(std::memory_order_acquire)) return;  // Line 175 - This one looks OK

        auto now = std::chrono::steady_clock::now();
        int evicted_count = 0;

        auto it = std::remove_if(idle_connections_.begin(), idle_connections_.end(), [&](const internal::BoltPhysicalConnection::PooledConnection& conn_ptr) {
            if (!conn_ptr) return true;

            bool evict = false;
            std::string reason;

            if (config_.max_connection_lifetime_ms > 0) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - conn_ptr->get_creation_timestamp()).count() > static_cast<long long>(config_.max_connection_lifetime_ms)) {
                    evict = true;
                    reason = "max lifetime";
                }
            }
            if (!evict && config_.idle_timeout_ms > 0) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - conn_ptr->get_last_used_timestamp()).count() > static_cast<long long>(config_.idle_timeout_ms)) {
                    evict = true;
                    reason = "idle timeout";
                }
            }
            if (!evict && conn_ptr->is_defunct()) {
                evict = true;
                reason = "found defunct in pool";
            }

            if (evict) {
                if (config_.logger) config_.logger->info("[PoolEvictor] Evicting conn {} due to {}.", conn_ptr->get_id(), reason);
                conn_ptr->terminate(false);
                return true;
            }
            return false;
        });

        evicted_count = std::distance(it, idle_connections_.end());
        if (evicted_count > 0) {
            idle_connections_.erase(it, idle_connections_.end());
            total_connections_created_ -= evicted_count;
            if (config_.logger) config_.logger->debug("[PoolEvictor] Evicted {} connections. Idle now: {}. Total created: {}", evicted_count, idle_connections_.size(), total_connections_created_);
            pool_condition_.notify_all();
        }
    }

}  // namespace neo4j_bolt_transport