#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_CONNECTION_CONFIG_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_CONNECTION_CONFIG_H

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"                     // For Value, Version
#include "neo4j_bolt_transport/config/transport_config.h"  // For AuthTokenVariant, EncryptionStrategy

namespace neo4j_bolt_transport {
    namespace internal {

        struct BoltConnectionConfig {
            std::string target_host;
            uint16_t target_port;

            config::AuthTokenVariant auth_token;
            std::string user_agent_for_hello;
            boltprotocol::HelloMessageParams::BoltAgentInfo bolt_agent_info_for_hello;

            bool encryption_enabled = false;
            // 使用 TransportConfig中的 EncryptionStrategy
            config::TransportConfig::EncryptionStrategy resolved_encryption_strategy = config::TransportConfig::EncryptionStrategy::NEGOTIATE_FROM_URI_SCHEME;
            std::vector<std::string> trusted_certificates_pem_files;
            std::optional<std::string> client_certificate_pem_file;
            std::optional<std::string> client_private_key_pem_file;
            std::optional<std::string> client_private_key_password;
            bool hostname_verification_enabled = true;

            uint32_t tcp_connect_timeout_ms = 5000;
            bool socket_keep_alive_enabled = true;
            bool tcp_no_delay_enabled = true;            // <--- 新增，与 TransportConfig 同步
            uint32_t bolt_handshake_timeout_ms = 10000;  // 可以考虑从 TransportConfig 获取

            std::optional<std::map<std::string, boltprotocol::Value>> hello_routing_context;

            // 新增：用于 Bolt 握手的首选版本列表
            std::optional<std::vector<boltprotocol::versions::Version>> preferred_bolt_versions;
        };

    }  // namespace internal
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_INTERNAL_BOLT_CONNECTION_CONFIG_H