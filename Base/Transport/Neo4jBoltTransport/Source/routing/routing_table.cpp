#include "neo4j_bolt_transport/routing/routing_table.h"

#include <algorithm>  // For std::remove

namespace neo4j_bolt_transport {
    namespace routing {

        RoutingTable::RoutingTable(std::string db_context_key, std::chrono::seconds ttl_seconds)
            : database_context_key_(std::move(db_context_key)),
              last_updated_time_(std::chrono::steady_clock::time_point::min()),  // Stale by default
              ttl_(ttl_seconds) {
        }

        std::optional<ServerAddress> RoutingTable::get_server(ServerRole role) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_stale()) {
                return std::nullopt;
            }

            std::vector<ServerAddress>* server_list = nullptr;
            std::atomic<std::size_t>* index_ptr = nullptr;

            switch (role) {
                case ServerRole::ROUTER:
                    server_list = &routers_;
                    index_ptr = &next_router_index_;
                    break;
                case ServerRole::READER:
                    server_list = &readers_;
                    index_ptr = &next_reader_index_;
                    break;
                case ServerRole::WRITER:
                    server_list = &writers_;
                    index_ptr = &next_writer_index_;
                    break;
            }

            if (!server_list || server_list->empty()) {
                return std::nullopt;
            }

            std::size_t current_index = index_ptr->fetch_add(1, std::memory_order_relaxed);
            return (*server_list)[current_index % server_list->size()];
        }

        boltprotocol::BoltError RoutingTable::update(const std::vector<ServerAddress>& new_routers, const std::vector<ServerAddress>& new_readers, const std::vector<ServerAddress>& new_writers, std::chrono::seconds new_ttl_seconds) {
            std::lock_guard<std::mutex> lock(mutex_);

            // It's crucial that ROUTE message provides absolute lists, not diffs.
            routers_ = new_routers;
            readers_ = new_readers;
            writers_ = new_writers;
            ttl_ = new_ttl_seconds;
            last_updated_time_ = std::chrono::steady_clock::now();

            next_reader_index_ = 0;
            next_writer_index_ = 0;
            next_router_index_ = 0;  // Reset router index as well

            if (routers_.empty() && (readers_.empty() || writers_.empty())) {
                // A routing table must have routers, or if it's a single-instance-like scenario
                // (no explicit routers), it must at least have readers and writers.
                // If all are empty after an update, it's problematic.
                mark_as_stale();                                         // Mark as stale to force re-fetch or error out
                return boltprotocol::BoltError::INVALID_MESSAGE_FORMAT;  // Or a more specific routing error
            }

            return boltprotocol::BoltError::SUCCESS;
        }

        bool RoutingTable::is_stale() const {
            // No lock needed for reading const members or time_point if access is atomic enough,
            // but ttl_ could change. For safety, or if ttl_ wasn't const, use lock.
            // Here, last_updated_time_ is std::chrono, reads are usually atomic. ttl_ is const after construction until update.
            // For simplicity with mutex_:
            // std::lock_guard<std::mutex> lock(mutex_);
            if (last_updated_time_ == std::chrono::steady_clock::time_point::min()) return true;  // Never updated
            return std::chrono::steady_clock::now() > (last_updated_time_ + ttl_);
        }

        void RoutingTable::mark_as_stale() {
            std::lock_guard<std::mutex> lock(mutex_);
            last_updated_time_ = std::chrono::steady_clock::time_point::min();
        }

        const std::vector<ServerAddress>& RoutingTable::get_routers() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return routers_;
        }

        void RoutingTable::forget_server(const ServerAddress& address) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto remove_addr = [&](std::vector<ServerAddress>& vec) {
                vec.erase(std::remove(vec.begin(), vec.end(), address), vec.end());
            };
            remove_addr(routers_);
            remove_addr(readers_);
            remove_addr(writers_);

            // If forgetting a server makes a critical list empty, table might become stale faster
            if ((database_context_key_ != "system" && (readers_.empty() || writers_.empty())) || routers_.empty()) {
                // For simplicity, just mark stale. More complex logic could try other servers first.
                mark_as_stale();
            }
        }

    }  // namespace routing
}  // namespace neo4j_bolt_transport