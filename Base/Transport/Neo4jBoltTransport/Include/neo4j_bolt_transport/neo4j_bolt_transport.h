#ifndef NEO4J_BOLT_TRANSPORT_NEO4J_BOLT_TRANSPORT_H
#define NEO4J_BOLT_TRANSPORT_NEO4J_BOLT_TRANSPORT_H

#include <atomic>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>  // For eviction_timer_
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "config/transport_config.h"
#include "internal/bolt_physical_connection.h"
#include "routing/routing_table.h"
#include "routing/server_address.h"
#include "session_handle.h"
#include "uri/parsed_uri.h"

// 暂时移除异步相关的 Boost.Asio include，因为具体实现被推迟
// #include <boost/asio/co_spawn.hpp>
// #include <boost/asio/detached.hpp>
// #include <boost/asio/awaitable.hpp>

namespace neo4j_bolt_transport {

    namespace config {
        struct SessionParameters;
    }
    class AsyncSessionHandle;  // 前向声明，即使实现被推迟，头文件可能仍需它

    class Neo4jBoltTransport {
      public:
        explicit Neo4jBoltTransport(config::TransportConfig config);
        ~Neo4jBoltTransport();

        Neo4jBoltTransport(const Neo4jBoltTransport&) = delete;
        Neo4jBoltTransport& operator=(const Neo4jBoltTransport&) = delete;
        Neo4jBoltTransport(Neo4jBoltTransport&&) = delete;
        Neo4jBoltTransport& operator=(Neo4jBoltTransport&&) = delete;

        // --- Synchronous API ---
        boltprotocol::BoltError verify_connectivity();
        std::pair<std::pair<boltprotocol::BoltError, std::string>, SessionHandle> acquire_session(const config::SessionParameters& params);
        void close();

        const config::TransportConfig& get_config() const {
            return config_;
        }
        boost::asio::io_context& get_io_context() {
            return io_context_;
        }

        void release_connection(internal::BoltPhysicalConnection::PooledConnection connection, bool mark_as_healthy = true);

        // --- Asynchronous API (占位符) ---
        // 实际返回类型可能是 boost::asio::awaitable<...>
        // 为了编译通过（即使AsyncSessionHandle不完整），暂时用一个可以构造的类型
        // 或者完全注释掉，直到 AsyncSessionHandle 准备好
        // boost::asio::awaitable<std::pair<std::pair<boltprotocol::BoltError, std::string>, AsyncSessionHandle>>
        // acquire_async_session(const config::SessionParameters& params);

      private:
        // --- 连接池与路由辅助函数 ---
        std::pair<boltprotocol::BoltError, internal::BoltPhysicalConnection::PooledConnection> _acquire_pooled_connection(const routing::ServerAddress& target_address, const std::optional<std::string>& database_name_hint);

        // boost::asio::awaitable<std::pair<boltprotocol::BoltError, internal::BoltPhysicalConnection::PooledConnection>>
        // _acquire_async_pooled_connection(const routing::ServerAddress& target_address, const std::optional<std::string>& database_name_hint);

        std::pair<boltprotocol::BoltError, routing::ServerAddress> _get_server_address_for_session(const config::SessionParameters& params, routing::ServerRole role_hint);
        std::shared_ptr<routing::RoutingTable> _get_or_fetch_routing_table(const std::string& database_name, const std::optional<std::string>& impersonated_user);
        boltprotocol::BoltError _fetch_and_update_routing_table(std::shared_ptr<routing::RoutingTable> table_to_update, const std::vector<routing::ServerAddress>& initial_routers_for_context, const std::string& database_name_hint, const std::optional<std::string>& impersonated_user_hint);
        void _handle_routing_failure(const routing::ServerAddress& failed_address, const std::string& database_context_key);

        void _evict_stale_connections_task();
        internal::BoltConnectionConfig _create_physical_connection_config(const routing::ServerAddress& target_address, const std::optional<std::map<std::string, boltprotocol::Value>>& routing_context_for_hello) const;

        // 静态私有辅助函数，用于生成路由上下文键
        static std::string _make_routing_context_key(const std::string& database_name, const std::optional<std::string>& impersonated_user);

        config::TransportConfig config_;
        uri::ParsedUri parsed_initial_uri_;

        boost::asio::io_context io_context_;
        std::unique_ptr<std::thread> io_thread_;
        bool own_io_thread_ = false;
        std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard_;

        std::deque<internal::BoltPhysicalConnection::PooledConnection> idle_connections_;
        std::size_t total_connections_currently_pooled_ = 0;
        std::size_t total_connections_ever_created_ = 0;
        std::mutex pool_mutex_;
        std::condition_variable pool_condition_;
        std::atomic<bool> closing_{false};

        std::string finalized_user_agent_;
        boltprotocol::HelloMessageParams::BoltAgentInfo finalized_bolt_agent_info_;

        std::map<std::string, std::shared_ptr<routing::RoutingTable>> routing_tables_;
        std::mutex routing_table_mutex_;

        std::unique_ptr<boost::asio::steady_timer> eviction_timer_;
    };

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_NEO4J_BOLT_TRANSPORT_H