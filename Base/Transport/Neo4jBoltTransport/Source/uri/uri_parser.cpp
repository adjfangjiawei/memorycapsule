#include "neo4j_bolt_transport/uri/uri_parser.h"

#include <algorithm>  // For std::transform, std::remove_if
#include <cctype>     // For std::tolower, std::isspace
#include <stdexcept>  // For std::stoi, std::stoul exceptions

namespace neo4j_bolt_transport {
    namespace uri {

        // Basic URL decoding (handles %XY and +)
        static std::string url_decode_component(const std::string& encoded) {
            std::string decoded;
            decoded.reserve(encoded.length());
            for (size_t i = 0; i < encoded.length(); ++i) {
                if (encoded[i] == '%' && i + 2 < encoded.length()) {
                    try {
                        std::string hex = encoded.substr(i + 1, 2);
                        char c = static_cast<char>(std::stoi(hex, nullptr, 16));
                        decoded += c;
                        i += 2;
                    } catch (const std::invalid_argument&) {  // Not a hex number
                        decoded += '%';                       // Treat as literal '%'
                    } catch (const std::out_of_range&) {      // Hex value too large for char
                        decoded += '%';                       // Treat as literal '%'
                    }
                } else if (encoded[i] == '+') {
                    decoded += ' ';
                } else {
                    decoded += encoded[i];
                }
            }
            return decoded;
        }

        // Helper to trim leading/trailing whitespace
        static std::string trim_whitespace(const std::string& s) {
            auto first = std::find_if_not(s.begin(), s.end(), [](unsigned char c) {
                return std::isspace(c);
            });
            if (first == s.end()) return "";  // String is all whitespace
            auto last = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char c) {
                            return std::isspace(c);
                        }).base();
            return std::string(first, last);
        }

        boltprotocol::BoltError UriParser::parse(const std::string& uri_string, ParsedUri& out_parsed_uri) {
            out_parsed_uri = {};  // Reset
            out_parsed_uri.input_uri = uri_string;

            if (uri_string.empty()) {
                return boltprotocol::BoltError::INVALID_ARGUMENT;
            }

            // 1. Scheme
            size_t scheme_end_pos = uri_string.find("://");
            if (scheme_end_pos == std::string::npos || scheme_end_pos == 0) {
                return boltprotocol::BoltError::INVALID_ARGUMENT;
            }
            out_parsed_uri.scheme = uri_string.substr(0, scheme_end_pos);
            std::transform(out_parsed_uri.scheme.begin(), out_parsed_uri.scheme.end(), out_parsed_uri.scheme.begin(), [](unsigned char c) {
                return std::tolower(c);
            });

            std::string remaining_uri = uri_string.substr(scheme_end_pos + 3);
            if (remaining_uri.empty() || remaining_uri[0] == '/' || remaining_uri[0] == '?') {  // Authority must exist
                return boltprotocol::BoltError::INVALID_ARGUMENT;
            }

            // 2. Authority (and userinfo within it)
            size_t authority_terminator_pos = remaining_uri.find_first_of("/?");
            std::string authority_part_full = remaining_uri.substr(0, authority_terminator_pos);

            std::string path_and_query_part;
            if (authority_terminator_pos != std::string::npos) {
                path_and_query_part = remaining_uri.substr(authority_terminator_pos);
            }

            // Userinfo (username:password@)
            size_t userinfo_terminator_pos = authority_part_full.find('@');
            std::string host_port_list_str = authority_part_full;

            if (userinfo_terminator_pos != std::string::npos) {
                std::string userinfo_str = authority_part_full.substr(0, userinfo_terminator_pos);
                host_port_list_str = authority_part_full.substr(userinfo_terminator_pos + 1);
                if (host_port_list_str.empty()) return boltprotocol::BoltError::INVALID_ARGUMENT;  // Host missing after @

                size_t password_delim_pos = userinfo_str.find(':');
                if (password_delim_pos != std::string::npos) {
                    out_parsed_uri.username_from_uri = url_decode_component(userinfo_str.substr(0, password_delim_pos));
                    out_parsed_uri.password_from_uri = url_decode_component(userinfo_str.substr(password_delim_pos + 1));
                } else {
                    out_parsed_uri.username_from_uri = url_decode_component(userinfo_str);
                }
            }
            if (host_port_list_str.empty()) return boltprotocol::BoltError::INVALID_ARGUMENT;  // Host(s) part is mandatory

            // Parse host(s) and port(s)
            // Host part can be a comma-separated list for routing schemes
            size_t current_token_pos = 0;
            while (current_token_pos < host_port_list_str.length()) {
                size_t next_host_separator = host_port_list_str.find(',', current_token_pos);
                std::string current_host_port_token = trim_whitespace(host_port_list_str.substr(current_token_pos, next_host_separator - current_token_pos));
                if (current_host_port_token.empty()) {  // Handles cases like ",," or leading/trailing commas
                    if (next_host_separator == std::string::npos) break;
                    current_token_pos = next_host_separator + 1;
                    continue;
                }

                std::string current_host_str;
                uint16_t current_port_val = 0;  // Default based on scheme later if not specified here

                size_t port_separator_pos = current_host_port_token.rfind(':');
                size_t ipv6_bracket_end_pos = current_host_port_token.rfind(']');

                if (port_separator_pos != std::string::npos && (ipv6_bracket_end_pos == std::string::npos || port_separator_pos > ipv6_bracket_end_pos)) {
                    // Port is specified
                    current_host_str = trim_whitespace(current_host_port_token.substr(0, port_separator_pos));
                    std::string port_str = trim_whitespace(current_host_port_token.substr(port_separator_pos + 1));
                    if (port_str.empty()) return boltprotocol::BoltError::INVALID_ARGUMENT;  // Port num missing after ':'
                    try {
                        unsigned long p_val = std::stoul(port_str);
                        if (p_val == 0 || p_val > 65535) return boltprotocol::BoltError::INVALID_ARGUMENT;  // Invalid port range
                        current_port_val = static_cast<uint16_t>(p_val);
                    } catch (const std::exception&) {                      // std::invalid_argument or std::out_of_range
                        return boltprotocol::BoltError::INVALID_ARGUMENT;  // Port not a number or out of range
                    }
                } else {
                    // No port specified for this token
                    current_host_str = current_host_port_token;
                }

                // Remove IPv6 brackets if present
                if (current_host_str.length() >= 2 && current_host_str.front() == '[' && current_host_str.back() == ']') {
                    current_host_str = current_host_str.substr(1, current_host_str.length() - 2);
                }
                if (current_host_str.empty()) return boltprotocol::BoltError::INVALID_ARGUMENT;  // Host cannot be empty

                out_parsed_uri.hosts_with_ports.emplace_back(current_host_str, current_port_val);

                if (next_host_separator == std::string::npos) break;
                current_token_pos = next_host_separator + 1;
            }
            if (out_parsed_uri.hosts_with_ports.empty()) return boltprotocol::BoltError::INVALID_ARGUMENT;  // No valid host found

            // 3. Query Parameters (Path component is usually ignored or used for specific DB in some drivers, simplified here)
            if (!path_and_query_part.empty()) {
                size_t query_start_pos = path_and_query_part.find('?');
                if (query_start_pos != std::string::npos) {
                    std::string query_string = path_and_query_part.substr(query_start_pos + 1);
                    size_t current_param_pos = 0;
                    while (current_param_pos < query_string.length()) {
                        size_t next_amp_pos = query_string.find('&', current_param_pos);
                        std::string param_pair_str = query_string.substr(current_param_pos, next_amp_pos - current_param_pos);
                        size_t eq_pos = param_pair_str.find('=');
                        if (eq_pos != std::string::npos) {
                            std::string key = trim_whitespace(url_decode_component(param_pair_str.substr(0, eq_pos)));
                            std::string value = trim_whitespace(url_decode_component(param_pair_str.substr(eq_pos + 1)));
                            if (!key.empty()) out_parsed_uri.query_parameters[key] = value;
                        } else if (!param_pair_str.empty()) {
                            std::string key = trim_whitespace(url_decode_component(param_pair_str));
                            if (!key.empty()) out_parsed_uri.query_parameters[key] = "";
                        }
                        if (next_amp_pos == std::string::npos) break;
                        current_param_pos = next_amp_pos + 1;
                    }
                }
            }

            // Apply scheme-specific logic and default ports
            uint16_t default_port_for_scheme = 0;

            if (out_parsed_uri.scheme == "bolt") {
                out_parsed_uri.tls_enabled_by_scheme = false;
                default_port_for_scheme = ParsedUri::DEFAULT_BOLT_PORT;
            } else if (out_parsed_uri.scheme == "bolt+s") {
                out_parsed_uri.tls_enabled_by_scheme = true;
                out_parsed_uri.trust_strategy_hint = ParsedUri::SchemeTrustStrategy::SYSTEM_CAS;
                default_port_for_scheme = ParsedUri::DEFAULT_BOLTS_PORT;
            } else if (out_parsed_uri.scheme == "bolt+ssc") {
                out_parsed_uri.tls_enabled_by_scheme = true;
                out_parsed_uri.trust_strategy_hint = ParsedUri::SchemeTrustStrategy::TRUST_ALL_CERTS;
                default_port_for_scheme = ParsedUri::DEFAULT_BOLTS_PORT;
            } else if (out_parsed_uri.scheme == "neo4j") {
                out_parsed_uri.is_routing_scheme = true;
                out_parsed_uri.tls_enabled_by_scheme = false;  // Routing table will dictate TLS for resolved servers
                default_port_for_scheme = ParsedUri::DEFAULT_BOLT_PORT;
            } else if (out_parsed_uri.scheme == "neo4j+s") {
                out_parsed_uri.is_routing_scheme = true;
                out_parsed_uri.tls_enabled_by_scheme = true;  // Initial connection to router uses TLS
                out_parsed_uri.trust_strategy_hint = ParsedUri::SchemeTrustStrategy::SYSTEM_CAS;
                default_port_for_scheme = ParsedUri::DEFAULT_BOLTS_PORT;
            } else if (out_parsed_uri.scheme == "neo4j+ssc") {
                out_parsed_uri.is_routing_scheme = true;
                out_parsed_uri.tls_enabled_by_scheme = true;  // Initial connection to router uses TLS
                out_parsed_uri.trust_strategy_hint = ParsedUri::SchemeTrustStrategy::TRUST_ALL_CERTS;
                default_port_for_scheme = ParsedUri::DEFAULT_BOLTS_PORT;
            } else {
                return boltprotocol::BoltError::INVALID_ARGUMENT;  // Unknown scheme
            }

            // Apply default port if any host_with_port has port 0
            for (auto& host_port_pair : out_parsed_uri.hosts_with_ports) {
                if (host_port_pair.second == 0) {
                    if (default_port_for_scheme == 0) return boltprotocol::BoltError::INVALID_ARGUMENT;  // Scheme needs explicit port
                    host_port_pair.second = default_port_for_scheme;
                }
            }

            out_parsed_uri.is_valid = true;
            return boltprotocol::BoltError::SUCCESS;
        }

    }  // namespace uri
}  // namespace neo4j_bolt_transport