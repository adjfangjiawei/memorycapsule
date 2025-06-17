#ifndef NEO4J_BOLT_TRANSPORT_ERROR_NEO4J_ERROR_UTIL_H
#define NEO4J_BOLT_TRANSPORT_ERROR_NEO4J_ERROR_UTIL_H

#include <optional>
#include <string>

#include "boltprotocol/message_defs.h"  // For BoltError and FailureMessageParams

namespace neo4j_bolt_transport {
    namespace error {

        // Creates a detailed error message string from FailureMessageParams
        inline std::string format_server_failure(const boltprotocol::FailureMessageParams& failure_params) {
            std::string server_code = "Unknown.Error";
            std::string server_message = "An error occurred on the server.";

            auto extract_string_from_value = [](const boltprotocol::Value& val) -> std::optional<std::string> {
                if (std::holds_alternative<std::string>(val)) {
                    return std::get<std::string>(val);
                }
                return std::nullopt;
            };

            auto it_code = failure_params.metadata.find("neo4j_code");  // Bolt 5.7+
            if (it_code == failure_params.metadata.end() || !extract_string_from_value(it_code->second).has_value()) {
                it_code = failure_params.metadata.find("code");  // Legacy
            }
            if (it_code != failure_params.metadata.end()) {
                if (auto code_opt = extract_string_from_value(it_code->second)) {
                    server_code = *code_opt;
                }
            }

            auto it_msg = failure_params.metadata.find("message");
            if (it_msg != failure_params.metadata.end()) {
                if (auto msg_opt = extract_string_from_value(it_msg->second)) {
                    server_message = *msg_opt;
                }
            }

            return "[" + server_code + "] " + server_message;
        }

        // Converts BoltError enum to a human-readable string (basic version)
        inline std::string bolt_error_to_string(boltprotocol::BoltError err_code) {
            switch (err_code) {
                case boltprotocol::BoltError::SUCCESS:
                    return "SUCCESS";
                case boltprotocol::BoltError::UNKNOWN_ERROR:
                    return "UNKNOWN_ERROR";
                case boltprotocol::BoltError::INVALID_ARGUMENT:
                    return "INVALID_ARGUMENT";
                case boltprotocol::BoltError::SERIALIZATION_ERROR:
                    return "SERIALIZATION_ERROR";
                case boltprotocol::BoltError::DESERIALIZATION_ERROR:
                    return "DESERIALIZATION_ERROR";
                case boltprotocol::BoltError::INVALID_MESSAGE_FORMAT:
                    return "INVALID_MESSAGE_FORMAT";
                case boltprotocol::BoltError::UNSUPPORTED_PROTOCOL_VERSION:
                    return "UNSUPPORTED_PROTOCOL_VERSION";
                case boltprotocol::BoltError::NETWORK_ERROR:
                    return "NETWORK_ERROR";
                case boltprotocol::BoltError::HANDSHAKE_FAILED:
                    return "HANDSHAKE_FAILED";
                // ... add all other BoltError codes ...
                default:
                    return "UNRECOGNIZED_BOLT_ERROR (" + std::to_string(static_cast<int>(err_code)) + ")";
            }
        }

        // Combines a BoltError with a context message and potentially a server failure message
        inline std::string format_error_message(const std::string& context, boltprotocol::BoltError err_code, const std::optional<std::string>& server_failure_detail = std::nullopt) {
            std::string msg = context + ": " + bolt_error_to_string(err_code) + " (code " + std::to_string(static_cast<int>(err_code)) + ")";
            if (server_failure_detail && !server_failure_detail->empty()) {
                msg += "; Server detail: " + *server_failure_detail;
            }
            return msg;
        }

    }  // namespace error
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_ERROR_NEO4J_ERROR_UTIL_H