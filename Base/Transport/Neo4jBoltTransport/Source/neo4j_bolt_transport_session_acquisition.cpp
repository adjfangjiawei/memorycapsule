#include "neo4j_bolt_transport/config/session_parameters.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"
#include "neo4j_bolt_transport/session_handle.h"
// #include "neo4j_bolt_transport/async_session_handle.h" // 暂时移除
#include <iostream>
#include <utility>  // For std::move

#include "neo4j_bolt_transport/error/neo4j_error_util.h"

namespace neo4j_bolt_transport {

    // --- Neo4jBoltTransport - acquire_session (同步) ---
    std::pair<std::pair<boltprotocol::BoltError, std::string>, SessionHandle> Neo4jBoltTransport::acquire_session(const config::SessionParameters& params) {
        if (closing_.load(std::memory_order_acquire)) {
            std::string err_msg = "尝试在关闭的 transport 上获取会话。";
            if (config_.logger) config_.logger->warn("[SessionAcq] {}", err_msg);
            return {{boltprotocol::BoltError::UNKNOWN_ERROR, err_msg}, SessionHandle(this, nullptr, params)};
        }

        if (config_.logger) config_.logger->debug("[SessionAcq] 尝试获取会话，数据库: '{}', 访问模式: {}", params.database_name.value_or("<默认>"), (params.default_access_mode == config::AccessMode::READ ? "READ" : "WRITE"));

        routing::ServerRole role_hint = (params.default_access_mode == config::AccessMode::READ) ? routing::ServerRole::READER : routing::ServerRole::WRITER;

        auto [addr_err, target_server_address] = _get_server_address_for_session(params, role_hint);

        if (addr_err != boltprotocol::BoltError::SUCCESS || target_server_address.host.empty()) {
            std::string err_msg = "无法为会话确定服务器地址 (DB: " + params.database_name.value_or("<默认>") + ", 角色: " + (role_hint == routing::ServerRole::READER ? "R" : "W") + "): " + error::bolt_error_to_string(addr_err);
            if (!target_server_address.host.empty()) {
                err_msg += " (目标地址尝试: " + target_server_address.to_string() + ")";
            }
            if (config_.logger) config_.logger->error("[SessionAcq] {}", err_msg);
            return {{addr_err, err_msg}, SessionHandle(this, nullptr, params)};
        }

        auto [conn_err_code, pooled_conn] = _acquire_pooled_connection(target_server_address, params.database_name);

        if (conn_err_code != boltprotocol::BoltError::SUCCESS || !pooled_conn) {
            std::string err_msg = "无法从池中获取到 " + target_server_address.to_string() + " 的连接: (" + error::bolt_error_to_string(conn_err_code) + ")";
            if (config_.logger) config_.logger->error("[SessionAcq] {}", err_msg);

            if (config_.client_side_routing_enabled && conn_err_code == boltprotocol::BoltError::NETWORK_ERROR) {
                std::string db_name_for_routing_key = params.database_name.value_or("");
                // 调用 Neo4jBoltTransport 的静态私有方法
                _handle_routing_failure(target_server_address, Neo4jBoltTransport::_make_routing_context_key(db_name_for_routing_key, params.impersonated_user));
            }
            return {{conn_err_code, err_msg}, SessionHandle(this, nullptr, params)};
        }

        if (config_.logger) config_.logger->info("[SessionAcq] 会话已成功获取，使用连接 {} 到 {}", pooled_conn->get_id(), target_server_address.to_string());
        return {{boltprotocol::BoltError::SUCCESS, ""}, SessionHandle(this, std::move(pooled_conn), params)};
    }

    // acquire_async_session 的函数体暂时移除，只保留声明在头文件中（或也从头文件中移除，后续添加）
    /*
    boost::asio::awaitable<std::pair<std::pair<boltprotocol::BoltError, std::string>, AsyncSessionHandle>>
    Neo4jBoltTransport::acquire_async_session(const config::SessionParameters& params) {
        // ... (之前的占位符或完全移除) ...
        if (config_.logger) config_.logger->debug("[SessionAcqAsync] 异步获取会话 (当前未实现)。");
        co_return std::make_pair(std::make_pair(boltprotocol::BoltError::UNKNOWN_ERROR, "Async session acquisition not implemented."), AsyncSessionHandle(this, nullptr, params));
    }
    */

}  // namespace neo4j_bolt_transport