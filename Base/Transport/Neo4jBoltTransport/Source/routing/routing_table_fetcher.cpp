#include "boltprotocol/message_serialization.h"  // For ROUTE message
#include "boltprotocol/packstream_reader.h"      // For parsing ROUTE response
#include "neo4j_bolt_transport/error/neo4j_error_util.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // 核心的路由表获取和更新逻辑
    boltprotocol::BoltError Neo4jBoltTransport::_fetch_and_update_routing_table(std::shared_ptr<routing::RoutingTable> table_to_update,
                                                                                const std::vector<routing::ServerAddress>& routers_to_try,
                                                                                const std::string& database_name_hint,                       // 数据库名称，用于ROUTE消息的参数
                                                                                const std::optional<std::string>& impersonated_user_hint) {  // 模拟用户，用于ROUTE消息

        if (routers_to_try.empty()) {
            if (config_.logger) config_.logger->error("[RoutingFetcher] _fetch_and_update_routing_table: 没有提供路由器地址。");
            table_to_update->mark_as_stale();
            return boltprotocol::BoltError::INVALID_ARGUMENT;
        }
        if (!table_to_update) {
            if (config_.logger) config_.logger->error("[RoutingFetcher] _fetch_and_update_routing_table: table_to_update 为空。");
            return boltprotocol::BoltError::INVALID_ARGUMENT;
        }

        boltprotocol::RouteMessageParams route_params;
        // Bolt 4.3-4.4: route_params.routing_table_context (通常为空map)
        // Bolt 5.0+:  route_params.routing_table_context (可包含db, imp_user等)
        // 这里的 routing_table_context 是发送给服务器的，服务器用它来帮助确定它应该返回哪些地址（例如，在NAT后面）
        // 它通常是空的，或者包含客户端连接到此路由器的信息。
        // Neo4j Java驱动通常发送空map作为ROUTE请求的第一个参数（routing_context）。
        route_params.routing_table_context = {};  // 通常为空
        route_params.bookmarks = {};              // 通常在获取路由表时不传递书签

        // 确定ROUTE消息的格式版本
        boltprotocol::versions::Version version_for_route_message = boltprotocol::versions::V5_0;  // 默认使用较新版本
        if (!config_.preferred_bolt_versions.empty()) {
            // 使用驱动配置的最高优先级的 Bolt 版本来序列化 ROUTE 消息
            // 假设连接到的路由器能够理解这个版本
            version_for_route_message = config_.preferred_bolt_versions.front();
        }

        if (version_for_route_message >= boltprotocol::versions::Version(4, 4)) {  // Bolt 4.4+
            route_params.extra_for_v44_plus = std::map<std::string, boltprotocol::Value>();
            if (!database_name_hint.empty()) {
                (*route_params.extra_for_v44_plus)["db"] = database_name_hint;
            }
            // impersonated_user_hint 仅在 Bolt 5.1+ 的 ROUTE 消息中被正式支持于 extra map
            if (impersonated_user_hint.has_value() && !impersonated_user_hint->empty() && version_for_route_message >= boltprotocol::versions::Version(5, 1)) {
                (*route_params.extra_for_v44_plus)["imp_user"] = *impersonated_user_hint;
            }
        } else if (version_for_route_message == boltprotocol::versions::Version(4, 3)) {  // Bolt 4.3
            if (!database_name_hint.empty()) {
                route_params.db_name_for_v43 = database_name_hint;
            }
        } else {
            if (config_.logger) config_.logger->warn("[RoutingFetcher] ROUTE 消息格式不支持 Bolt 版本 {}.{}。", (int)version_for_route_message.major, (int)version_for_route_message.minor);
            table_to_update->mark_as_stale();
            return boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION;
        }

        std::vector<uint8_t> route_payload;
        boltprotocol::PackStreamWriter writer(route_payload);
        boltprotocol::BoltError err = boltprotocol::serialize_route_message(route_params, writer, version_for_route_message);
        if (err != boltprotocol::BoltError::SUCCESS) {
            if (config_.logger) config_.logger->error("[RoutingFetcher] 序列化 ROUTE 消息失败: {}", static_cast<int>(err));
            table_to_update->mark_as_stale();
            return err;
        }

        boltprotocol::SuccessMessageParams success_meta;
        boltprotocol::FailureMessageParams failure_meta;

        for (const auto& router_address_orig : routers_to_try) {
            if (closing_.load(std::memory_order_acquire)) return boltprotocol::BoltError::UNKNOWN_ERROR;  // 驱动正在关闭

            routing::ServerAddress router_address = router_address_orig;
            if (config_.server_address_resolver) {  // 应用地址解析器
                router_address = config_.server_address_resolver(router_address_orig);
            }

            if (config_.logger) config_.logger->debug("[RoutingFetcher] 尝试从路由器 {} (原始: {}) 获取路由表, 目标数据库: '{}'", router_address.to_string(), router_address_orig.to_string(), database_name_hint);

            // 为 HELLO 消息准备路由上下文，这应该是客户端连接到此特定路由器时所使用的地址。
            std::map<std::string, boltprotocol::Value> hello_routing_ctx;
            hello_routing_ctx["address"] = router_address.to_string();  // 发送解析后的地址

            internal::BoltConnectionConfig conn_conf = _create_physical_connection_config(router_address, hello_routing_ctx);
            auto temp_conn_logger = config_.get_or_create_logger("RouteConn");  // 单独的logger实例
            auto temp_conn = std::make_unique<internal::BoltPhysicalConnection>(std::move(conn_conf), io_context_, temp_conn_logger);

            if (temp_conn->establish() == boltprotocol::BoltError::SUCCESS) {
                // 检查协商的 Bolt 版本是否与序列化 ROUTE 消息时使用的版本兼容
                if (temp_conn->get_bolt_version() < boltprotocol::versions::Version(4, 3)) {
                    if (config_.logger) config_.logger->warn("[RoutingFetcher] 路由器 {} 使用的 Bolt 版本过低 ({}.{})，不支持现代 ROUTE 消息。", router_address.to_string(), (int)temp_conn->get_bolt_version().major, (int)temp_conn->get_bolt_version().minor);
                    temp_conn->terminate(true);
                    continue;  // 尝试下一个路由器
                }
                // 如果版本不匹配到需要重新序列化的程度，这里会更复杂。
                // 简单假设：如果连接成功，则尝试发送已序列化的ROUTE消息。

                boltprotocol::BoltError route_send_err = temp_conn->send_request_receive_summary(route_payload, success_meta, failure_meta);
                temp_conn->terminate(true);  // 关闭临时连接

                if (route_send_err == boltprotocol::BoltError::SUCCESS && temp_conn->get_last_error_code() == boltprotocol::BoltError::SUCCESS) {
                    auto rt_it = success_meta.metadata.find("rt");
                    if (rt_it != success_meta.metadata.end() && std::holds_alternative<std::shared_ptr<boltprotocol::BoltMap>>(rt_it->second)) {
                        const auto& rt_map_ptr = std::get<std::shared_ptr<boltprotocol::BoltMap>>(rt_it->second);
                        if (rt_map_ptr) {
                            const auto& rt_data = rt_map_ptr->pairs;
                            long long ttl_val_ll = 0;
                            std::vector<routing::ServerAddress> new_routers, new_readers, new_writers;

                            auto ttl_data_it = rt_data.find("ttl");
                            if (ttl_data_it != rt_data.end() && std::holds_alternative<int64_t>(ttl_data_it->second)) {
                                ttl_val_ll = std::get<int64_t>(ttl_data_it->second);
                            } else {
                                if (config_.logger) config_.logger->warn("[RoutingFetcher] ROUTE 响应中缺少 'ttl' 字段或类型不正确。");
                                // 可以设置一个默认值或标记错误
                            }
                            std::chrono::seconds ttl_val = std::chrono::seconds(ttl_val_ll > 0 ? ttl_val_ll : 300);  // 默认300s

                            auto servers_data_it = rt_data.find("servers");
                            if (servers_data_it != rt_data.end() && std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(servers_data_it->second)) {
                                const auto& servers_list_ptr = std::get<std::shared_ptr<boltprotocol::BoltList>>(servers_data_it->second);
                                if (servers_list_ptr) {
                                    for (const auto& server_item_val : servers_list_ptr->elements) {
                                        if (std::holds_alternative<std::shared_ptr<boltprotocol::BoltMap>>(server_item_val)) {
                                            const auto& server_map_ptr = std::get<std::shared_ptr<boltprotocol::BoltMap>>(server_item_val);
                                            if (server_map_ptr) {
                                                std::string role_str;
                                                std::vector<std::string> addresses_str_list;

                                                auto role_it = server_map_ptr->pairs.find("role");
                                                if (role_it != server_map_ptr->pairs.end() && std::holds_alternative<std::string>(role_it->second)) {
                                                    role_str = std::get<std::string>(role_it->second);
                                                }

                                                auto addrs_it = server_map_ptr->pairs.find("addresses");
                                                if (addrs_it != server_map_ptr->pairs.end() && std::holds_alternative<std::shared_ptr<boltprotocol::BoltList>>(addrs_it->second)) {
                                                    const auto& addrs_list_ptr = std::get<std::shared_ptr<boltprotocol::BoltList>>(addrs_it->second);
                                                    if (addrs_list_ptr) {
                                                        for (const auto& addr_val : addrs_list_ptr->elements) {
                                                            if (std::holds_alternative<std::string>(addr_val)) {
                                                                addresses_str_list.push_back(std::get<std::string>(addr_val));
                                                            }
                                                        }
                                                    }
                                                }

                                                auto parse_host_port = [&](const std::string& addr_str) -> std::optional<routing::ServerAddress> {
                                                    // 移除可能的方案前缀 (bolt://, neo4j:// etc.)
                                                    std::string clean_addr_str = addr_str;
                                                    size_t scheme_end = clean_addr_str.find("://");
                                                    if (scheme_end != std::string::npos) {
                                                        clean_addr_str = clean_addr_str.substr(scheme_end + 3);
                                                    }

                                                    size_t colon_pos = clean_addr_str.rfind(':');
                                                    if (colon_pos == std::string::npos || colon_pos == 0 || colon_pos == clean_addr_str.length() - 1) {
                                                        if (config_.logger) config_.logger->warn("[RoutingFetcher] 地址 '{}' 格式无效 (缺少端口或格式错误)", addr_str);
                                                        return std::nullopt;
                                                    }
                                                    try {
                                                        std::string host_part = clean_addr_str.substr(0, colon_pos);
                                                        // 移除 IPv6 地址的方括号
                                                        if (host_part.length() > 2 && host_part.front() == '[' && host_part.back() == ']') {
                                                            host_part = host_part.substr(1, host_part.length() - 2);
                                                        }
                                                        if (host_part.empty()) {
                                                            if (config_.logger) config_.logger->warn("[RoutingFetcher] 地址 '{}' 解析后主机部分为空", addr_str);
                                                            return std::nullopt;
                                                        }

                                                        uint16_t port = static_cast<uint16_t>(std::stoul(clean_addr_str.substr(colon_pos + 1)));
                                                        return routing::ServerAddress{host_part, port};
                                                    } catch (const std::exception& e) {
                                                        if (config_.logger) config_.logger->warn("[RoutingFetcher] 解析地址 '{}' 端口失败: {}", addr_str, e.what());
                                                        return std::nullopt;
                                                    }
                                                };

                                                std::vector<routing::ServerAddress>* target_list_ptr = nullptr;
                                                if (role_str == "ROUTE")
                                                    target_list_ptr = &new_routers;
                                                else if (role_str == "READ")
                                                    target_list_ptr = &new_readers;
                                                else if (role_str == "WRITE")
                                                    target_list_ptr = &new_writers;

                                                if (target_list_ptr) {
                                                    for (const auto& addr_str : addresses_str_list) {
                                                        if (auto sa_opt = parse_host_port(addr_str)) {
                                                            target_list_ptr->push_back(*sa_opt);
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    if (config_.logger) config_.logger->warn("[RoutingFetcher] ROUTE 响应中 'servers' 字段丢失或类型不正确。");
                                }
                            } else {
                                if (config_.logger) config_.logger->warn("[RoutingFetcher] ROUTE 响应中缺少 'rt' 字段或类型不正确。");
                                // 服务器可能返回了错误，但 send_request_receive_summary 认为协议级别是成功的
                                // 这种情况下，temp_conn->get_last_error_code() 可能不是 SUCCESS
                                if (temp_conn->get_last_error_code() != boltprotocol::BoltError::SUCCESS) {
                                    if (config_.logger) config_.logger->warn("[RoutingFetcher] ROUTE 消息的服务器响应指示错误: {}", temp_conn->get_last_error_message());
                                    // 标记为失败并尝试下一个路由器
                                } else {
                                    // 这是一个真正的协议格式问题
                                    table_to_update->mark_as_stale();
                                    return boltprotocol::BoltError::INVALID_MESSAGE_FORMAT;
                                }
                            }
                            // 成功解析并获得了新的服务器列表
                            boltprotocol::BoltError update_err = table_to_update->update(new_routers, new_readers, new_writers, ttl_val);
                            if (update_err == boltprotocol::BoltError::SUCCESS) {
                                if (config_.logger) {
                                    config_.logger->info(
                                        "[RoutingFetcher] 路由表 '{}' 已成功从 {} 更新。Routers: {}, Readers: {}, Writers: {}, TTL: {}s", table_to_update->get_database_context_key(), router_address.to_string(), new_routers.size(), new_readers.size(), new_writers.size(), ttl_val.count());
                                }
                            } else {
                                if (config_.logger) config_.logger->error("[RoutingFetcher] 更新路由表对象失败，尽管从服务器获取了数据。");
                            }
                            return update_err;  // 返回更新结果
                        }  // success_meta.metadata.find("rt")
                    }  // temp_conn->get_last_error_code() == SUCCESS
                }  // route_send_err == SUCCESS

                // 如果 route_send_err != SUCCESS 或 temp_conn->get_last_error_code() != SUCCESS
                if (config_.logger) {
                    config_.logger->warn(
                        "[RoutingFetcher] 向路由器 {} 发送 ROUTE 消息失败。内部错误: {}, 服务器错误: {} (消息: {})", router_address.to_string(), error::bolt_error_to_string(route_send_err), error::bolt_error_to_string(temp_conn->get_last_error_code()), temp_conn->get_last_error_message());
                }
                // 不需要特别处理，循环会尝试下一个路由器

            } else {  // temp_conn->establish() failed
                if (config_.logger) config_.logger->warn("[RoutingFetcher] 无法建立到路由器 {} (原始: {}) 的临时连接。错误: {}, 消息: {}", router_address.to_string(), router_address_orig.to_string(), static_cast<int>(temp_conn->get_last_error_code()), temp_conn->get_last_error_message());
            }
            // 如果到此路由器失败，则尝试下一个
        }

        if (config_.logger) config_.logger->error("[RoutingFetcher] 尝试了所有 {} 个路由器，但无法获取路由表 '{}'。", routers_to_try.size(), table_to_update->get_database_context_key());
        table_to_update->mark_as_stale();               // 标记为过时，因为刷新失败
        return boltprotocol::BoltError::NETWORK_ERROR;  // 或者更具体的 "Routing information unavailable"
    }

}  // namespace neo4j_bolt_transport