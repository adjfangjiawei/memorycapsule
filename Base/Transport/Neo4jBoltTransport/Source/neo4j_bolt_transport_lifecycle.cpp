#include <stdexcept>

#include "neo4j_bolt_transport/neo4j_bolt_transport.h"
#include "neo4j_bolt_transport/uri/uri_parser.h"

namespace neo4j_bolt_transport {

    Neo4jBoltTransport::Neo4jBoltTransport(config::TransportConfig a_config) : config_(std::move(a_config)) {
        if (uri::UriParser::parse(config_.uri_string, parsed_initial_uri_) != boltprotocol::BoltError::SUCCESS) {
            throw std::invalid_argument("Invalid Neo4j URI provided to TransportConfig: " + config_.uri_string);
        }
        // config_.apply_parsed_uri_settings(parsed_initial_uri_); // This should be done by TransportConfig constructor if it takes URI

        std::string driver_name_version = "Neo4jBoltTransportCpp/0.2.0-dev";
        finalized_bolt_agent_info_ = config_.bolt_agent_info;
        if (finalized_bolt_agent_info_.product.empty()) {
            finalized_bolt_agent_info_.product = driver_name_version;
        }

        if (config_.user_agent_override.empty()) {
            finalized_user_agent_ = finalized_bolt_agent_info_.product;
        } else {
            finalized_user_agent_ = config_.user_agent_override;
        }

        if (io_context_.stopped()) {
            io_context_.restart();
        }
        work_guard_ = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(io_context_));
    }

    Neo4jBoltTransport::~Neo4jBoltTransport() {
        close();
    }

    void Neo4jBoltTransport::close() {
        if (closing_) {
            return;
        }
        closing_ = true;

        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            // FIX: active_connections_ was removed. Idle connections are the ones to clear.
            // Any "active" connections are owned by SessionHandles; their destructors will call release_connection.
            for (auto& conn_ptr : idle_connections_) {
                if (conn_ptr) {
                    conn_ptr->terminate(true);
                }
            }
            idle_connections_.clear();
            total_connections_created_ = 0;  // Reset count as pool is being destroyed
        }
        pool_condition_.notify_all();

        if (work_guard_) {
            work_guard_->reset();
        }
        if (!io_context_.stopped()) {
            io_context_.stop();
        }
        if (io_thread_ && io_thread_->joinable()) {
            io_thread_->join();
        }
    }

    boltprotocol::BoltError Neo4jBoltTransport::verify_connectivity() {
        if (closing_) return boltprotocol::BoltError::UNKNOWN_ERROR;

        // FIX: Use fully qualified name
        internal::BoltPhysicalConnection::PooledConnection conn;
        boltprotocol::BoltError err = acquire_connection(conn);  // acquire_connection is private
        if (err != boltprotocol::BoltError::SUCCESS) {
            return err;
        }
        // If connection is acquired, it's "active" from pool's perspective until released.
        release_connection(std::move(conn), true);  // release_connection is public (for now)
        return boltprotocol::BoltError::SUCCESS;
    }

}  // namespace neo4j_bolt_transport