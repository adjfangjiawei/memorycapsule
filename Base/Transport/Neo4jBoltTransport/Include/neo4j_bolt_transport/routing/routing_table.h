#ifndef NEO4J_BOLT_TRANSPORT_ROUTING_ROUTING_TABLE_H
#define NEO4J_BOLT_TRANSPORT_ROUTING_ROUTING_TABLE_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"  // For BoltError (used in status)
#include "server_address.h"

namespace neo4j_bolt_transport {
    namespace routing {

        enum class ServerRole { ROUTER, READER, WRITER };

        class RoutingTable {
          public:
            RoutingTable(std::string db_context_key, std::chrono::seconds ttl_seconds);

            // Tries to get a server for the given role.
            // Returns nullopt if no suitable server or table is stale.
            std::optional<ServerAddress> get_server(ServerRole role);

            // Updates the table with new data from a ROUTE message response.
            // Returns BoltError::SUCCESS or an error code if parsing fails.
            boltprotocol::BoltError update(const std::vector<ServerAddress>& new_routers, const std::vector<ServerAddress>& new_readers, const std::vector<ServerAddress>& new_writers, std::chrono::seconds new_ttl_seconds);

            bool is_stale() const;
            void mark_as_stale();  // Forcefully mark as stale, e.g., after a connection error

            const std::string& get_database_context_key() const {
                return database_context_key_;
            }
            const std::vector<ServerAddress>& get_routers() const;

            // Remove a server from all lists (e.g., if it becomes unreachable)
            void forget_server(const ServerAddress& address);

          private:
            std::string database_context_key_;  // e.g., "mydatabase@user" or "system"
            std::vector<ServerAddress> routers_;
            std::vector<ServerAddress> readers_;
            std::vector<ServerAddress> writers_;

            std::chrono::steady_clock::time_point last_updated_time_;
            std::chrono::seconds ttl_;

            std::atomic<std::size_t> next_reader_index_ = 0;
            std::atomic<std::size_t> next_writer_index_ = 0;
            std::atomic<std::size_t> next_router_index_ = 0;  // For trying different routers

            mutable std::mutex mutex_;  // Protects access to server lists and indices
        };

    }  // namespace routing
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_ROUTING_ROUTING_TABLE_H