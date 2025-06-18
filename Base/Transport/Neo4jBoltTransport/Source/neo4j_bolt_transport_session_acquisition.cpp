#include <iostream>
#include <utility>

#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/config/session_parameters.h"
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/internal/bolt_connection_config.h"  // Needed for physical_conn_conf
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    // --- Neo4jBoltTransport - acquire_session (Synchronous) ---
    std::pair<std::pair<boltprotocol::BoltError, std::string>, SessionHandle> Neo4jBoltTransport::acquire_session(const config::SessionParameters& params) {
        if (closing_.load(std::memory_order_acquire)) {
            std::string err_msg = "Attempting to acquire session on a closing transport.";
            if (config_.logger) config_.logger->warn("[SessionAcq] {}", err_msg);
            return {{boltprotocol::BoltError::UNKNOWN_ERROR, err_msg}, SessionHandle(this, nullptr, params)};
        }

        if (config_.logger) config_.logger->debug("[SessionAcq] Attempting to acquire session, DB: '{}', Mode: {}", params.database_name.value_or("<default>"), (params.default_access_mode == config::AccessMode::READ ? "READ" : "WRITE"));

        routing::ServerRole role_hint = (params.default_access_mode == config::AccessMode::READ) ? routing::ServerRole::READER : routing::ServerRole::WRITER;

        auto [addr_err, target_server_address] = _get_server_address_for_session(params, role_hint);

        if (addr_err != boltprotocol::BoltError::SUCCESS || target_server_address.host.empty()) {
            std::string err_msg = "Cannot determine server address for session (DB: " + params.database_name.value_or("<default>") + ", Role: " + (role_hint == routing::ServerRole::READER ? "R" : "W") + "): " + error::bolt_error_to_string(addr_err);
            if (!target_server_address.host.empty()) {
                err_msg += " (Target address attempt: " + target_server_address.to_string() + ")";
            }
            if (config_.logger) config_.logger->error("[SessionAcq] {}", err_msg);
            return {{addr_err, err_msg}, SessionHandle(this, nullptr, params)};
        }

        auto [conn_err_code, pooled_conn] = _acquire_pooled_connection(target_server_address, params.database_name);

        if (conn_err_code != boltprotocol::BoltError::SUCCESS || !pooled_conn) {
            std::string err_msg = "Failed to acquire connection from pool to " + target_server_address.to_string() + ": (" + error::bolt_error_to_string(conn_err_code) + ")";
            if (pooled_conn && !pooled_conn->get_last_error_message().empty()) {
                err_msg += " Detail: " + pooled_conn->get_last_error_message();
            }
            if (config_.logger) config_.logger->error("[SessionAcq] {}", err_msg);

            if (config_.client_side_routing_enabled && conn_err_code == boltprotocol::BoltError::NETWORK_ERROR) {
                std::string db_name_for_routing_key = params.database_name.value_or("");
                _handle_routing_failure(target_server_address, _make_routing_context_key(db_name_for_routing_key, params.impersonated_user));
            }
            return {{conn_err_code, err_msg}, SessionHandle(this, nullptr, params)};
        }

        if (config_.logger) config_.logger->info("[SessionAcq] Session acquired successfully, using connection {} to {}", pooled_conn->get_id(), target_server_address.to_string());
        return {{boltprotocol::BoltError::SUCCESS, ""}, SessionHandle(this, std::move(pooled_conn), params)};
    }

    // --- Asynchronous Session Acquisition ---
    boost::asio::awaitable<std::pair<boltprotocol::BoltError, std::unique_ptr<internal::ActiveAsyncStreamContext>>> Neo4jBoltTransport::_acquire_active_async_stream_context(const routing::ServerAddress& target_address, const config::SessionParameters& session_params) {
        if (closing_.load(std::memory_order_acquire)) {
            if (config_.logger) config_.logger->warn("[AsyncSessionAcq] Attempt to acquire async stream context on closing transport for target {}.", target_address.to_string());
            co_return std::make_pair(boltprotocol::BoltError::UNKNOWN_ERROR, nullptr);
        }

        std::optional<std::map<std::string, boltprotocol::Value>> hello_routing_ctx_opt;
        if (config_.client_side_routing_enabled) {
            std::map<std::string, boltprotocol::Value> ctx_map;
            ctx_map["address"] = target_address.to_string();
            hello_routing_ctx_opt = ctx_map;
        }

        // Create the specific BoltConnectionConfig for this attempt
        internal::BoltConnectionConfig physical_conn_conf = _create_physical_connection_config(target_address, hello_routing_ctx_opt);
        std::shared_ptr<spdlog::logger> conn_logger = config_.get_or_create_logger("AsyncBoltConn");

        auto temp_conn_establisher = std::make_shared<internal::BoltPhysicalConnection>(physical_conn_conf, io_context_, conn_logger);  // Pass copy of physical_conn_conf

        if (config_.logger) config_.logger->debug("[AsyncSessionAcq] Attempting to establish async stream context to {} for DB '{}'", target_address.to_string(), session_params.database_name.value_or("<default>"));

        // establish_async returns a pair: {error_code, ActiveAsyncStreamContext_value_type}
        // The ActiveAsyncStreamContext_value_type is the one that needs to be moved into the unique_ptr
        auto [establish_err, stream_ctx_obj_val] = co_await temp_conn_establisher->establish_async();

        if (establish_err != boltprotocol::BoltError::SUCCESS) {
            if (config_.logger) config_.logger->error("[AsyncSessionAcq] Failed to establish async stream context to {}. Error: {}", target_address.to_string(), error::bolt_error_to_string(establish_err));
            if (config_.client_side_routing_enabled && establish_err == boltprotocol::BoltError::NETWORK_ERROR) {
                std::string db_name_for_routing_key = session_params.database_name.value_or("");
                _handle_routing_failure(target_address, _make_routing_context_key(db_name_for_routing_key, session_params.impersonated_user));
            }
            co_return std::make_pair(establish_err, nullptr);
        }

        if (config_.logger) config_.logger->info("[AsyncSessionAcq] Successfully established async stream context to {} with connection ID '{}'", target_address.to_string(), stream_ctx_obj_val.server_connection_id);

        // The ActiveAsyncStreamContext returned by establish_async already contains the correct BoltConnectionConfig
        // because establish_async itself constructs it using the BoltPhysicalConnection's config.
        // So, we just need to ensure that stream_ctx_obj_val *is* the ActiveAsyncStreamContext.
        // The establish_async signature is: awaitable<pair<BoltError, ActiveAsyncStreamContext>>
        // So, stream_ctx_obj_val is the ActiveAsyncStreamContext by value.
        co_return std::make_pair(boltprotocol::BoltError::SUCCESS, std::make_unique<internal::ActiveAsyncStreamContext>(std::move(stream_ctx_obj_val)));
    }

    boost::asio::awaitable<std::tuple<boltprotocol::BoltError, std::string, std::optional<AsyncSessionHandle>>> Neo4jBoltTransport::acquire_async_session(const config::SessionParameters& params) {
        if (closing_.load(std::memory_order_acquire)) {
            std::string err_msg = "Attempting to acquire async session on a closing transport.";
            if (config_.logger) config_.logger->warn("[AsyncSessionAcq] {}", err_msg);
            co_return std::make_tuple(boltprotocol::BoltError::UNKNOWN_ERROR, err_msg, std::nullopt);
        }

        if (config_.logger) config_.logger->debug("[AsyncSessionAcq] Acquiring async session for DB: '{}', Mode: {}", params.database_name.value_or("<default>"), (params.default_access_mode == config::AccessMode::READ ? "READ" : "WRITE"));

        routing::ServerRole role_hint = (params.default_access_mode == config::AccessMode::READ) ? routing::ServerRole::READER : routing::ServerRole::WRITER;

        auto [addr_err, target_server_address] = _get_server_address_for_session(params, role_hint);  // Still sync

        if (addr_err != boltprotocol::BoltError::SUCCESS || target_server_address.host.empty()) {
            std::string err_msg = "Cannot determine server address for async session (DB: " + params.database_name.value_or("<default>") + ", Role: " + (role_hint == routing::ServerRole::READER ? "R" : "W") + "): " + error::bolt_error_to_string(addr_err);
            if (config_.logger) config_.logger->error("[AsyncSessionAcq] {}", err_msg);
            co_return std::make_tuple(addr_err, err_msg, std::nullopt);
        }

        auto [ctx_err, stream_ctx_ptr] = co_await _acquire_active_async_stream_context(target_server_address, params);

        if (ctx_err != boltprotocol::BoltError::SUCCESS || !stream_ctx_ptr) {
            std::string err_msg = "Failed to acquire active async stream context for " + target_server_address.to_string() + ": (" + error::bolt_error_to_string(ctx_err) + ")";
            if (stream_ctx_ptr == nullptr && ctx_err != boltprotocol::BoltError::SUCCESS) {  // Add original error if stream is null due to it
                // This is already captured in err_msg
            } else if (stream_ctx_ptr == nullptr) {
                err_msg += " (Stream context pointer is null without specific error code from acquire)";
            }

            if (config_.logger) config_.logger->error("[AsyncSessionAcq] {}", err_msg);
            co_return std::make_tuple(ctx_err, err_msg, std::nullopt);
        }

        if (config_.logger) config_.logger->info("[AsyncSessionAcq] Async session acquired to {}, conn_id '{}'", target_server_address.to_string(), stream_ctx_ptr->server_connection_id);

        // Pass 'this' (Neo4jBoltTransport*) , params, and the owned unique_ptr to stream_context_
        AsyncSessionHandle async_session_handle(this, params, std::move(stream_ctx_ptr));
        co_return std::make_tuple(boltprotocol::BoltError::SUCCESS, "", std::make_optional(std::move(async_session_handle)));
    }

}  // namespace neo4j_bolt_transport