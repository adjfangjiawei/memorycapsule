// Neo4jBoltTransport.h (您提供的版本)
#ifndef NEO4J_BOLT_TRANSPORT_NEO4J_BOLT_TRANSPORT_H
#define NEO4J_BOLT_TRANSPORT_NEO4J_BOLT_TRANSPORT_H

#include <atomic>  // For std::atomic<bool> closing_
#include <condition_variable>
#include <deque>
#include <list>  // Not used, but present
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "config/transport_config.h"            // Defines config::TransportConfig
#include "internal/bolt_physical_connection.h"  // For PooledConnection
#include "session_handle.h"                     // Forward declared or defined
#include "uri/parsed_uri.h"                     // For ParsedUri

namespace neo4j_bolt_transport {

    // Forward declaration for SessionParameters if not fully defined by included headers
    namespace config {
        struct SessionParameters;
    }

    class Neo4jBoltTransport {
      public:
        explicit Neo4jBoltTransport(config::TransportConfig config);  // config::TransportConfig must be complete here
        ~Neo4jBoltTransport();

        Neo4jBoltTransport(const Neo4jBoltTransport&) = delete;
        Neo4jBoltTransport& operator=(const Neo4jBoltTransport&) = delete;
        Neo4jBoltTransport(Neo4jBoltTransport&&) = delete;
        Neo4jBoltTransport& operator=(Neo4jBoltTransport&&) = delete;

        boltprotocol::BoltError verify_connectivity();
        SessionHandle acquire_session(const config::SessionParameters& params);  // SessionParameters must be complete
        void close();

        // get_config() returns a const reference to config_
        // The type config::TransportConfig must have a public member 'logger'
        // or a public getter for the logger for this to work as intended in other files.
        const config::TransportConfig& get_config() const {
            return config_;
        }

        void release_connection(internal::BoltPhysicalConnection::PooledConnection connection, bool mark_as_healthy = true);

      private:
        boltprotocol::BoltError acquire_connection(internal::BoltPhysicalConnection::PooledConnection& out_connection, const std::string& for_database = "");
        void _evict_stale_connections();
        internal::BoltConnectionConfig _create_physical_connection_config(const std::string& target_host, uint16_t target_port) const;

        config::TransportConfig config_;  // Member of type config::TransportConfig
        uri::ParsedUri parsed_initial_uri_;

        boost::asio::io_context io_context_;
        std::unique_ptr<std::thread> io_thread_;
        std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard_;

        std::deque<internal::BoltPhysicalConnection::PooledConnection> idle_connections_;
        std::size_t total_connections_created_ = 0;  // Should probably be atomic if accessed without pool_mutex_
        std::mutex pool_mutex_;
        std::condition_variable pool_condition_;
        std::atomic<bool> closing_{false};  // Correctly atomic

        std::string finalized_user_agent_;
        boltprotocol::HelloMessageParams::BoltAgentInfo finalized_bolt_agent_info_;
    };

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_NEO4J_BOLT_TRANSPORT_H