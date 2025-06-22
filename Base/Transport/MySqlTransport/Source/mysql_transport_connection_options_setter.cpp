// cpporm_mysql_transport/mysql_transport_connection_options_setter.cpp
#include "cpporm_mysql_transport/mysql_transport_connection_options_setter.h"

#include <mysql/mysql.h>

#include <algorithm>  // For std::transform
#include <vector>     // For potential temporary buffers if needed

#include "cpporm_mysql_transport/mysql_transport_connection.h"  // To call setErrorFromMySQL directly on context

namespace cpporm_mysql_transport {

    MySqlTransportConnectionOptionsSetter::MySqlTransportConnectionOptionsSetter(MySqlTransportConnection* connection_context) : m_conn_ctx(connection_context) {
        if (!m_conn_ctx) {
            // This is a programming error, options setter cannot function without context.
            // No direct way to report error from constructor if m_conn_ctx itself is for error reporting.
            // Assume valid context.
        }
    }

    unsigned int MySqlTransportConnectionOptionsSetter::mapSslModeStringToValue(const std::string& mode_str) const {
        std::string upper_mode = mode_str;
        std::transform(upper_mode.begin(), upper_mode.end(), upper_mode.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });

        if (upper_mode == "DISABLED") return SSL_MODE_DISABLED;
        if (upper_mode == "PREFERRED") return SSL_MODE_PREFERRED;
        if (upper_mode == "REQUIRED") return SSL_MODE_REQUIRED;
        if (upper_mode == "VERIFY_CA") return SSL_MODE_VERIFY_CA;
        if (upper_mode == "VERIFY_IDENTITY") return SSL_MODE_VERIFY_IDENTITY;

        return SSL_MODE_PREFERRED;  // Default if unknown
    }

    bool MySqlTransportConnectionOptionsSetter::applyPreConnectOptions(MYSQL* mysql_handle, const MySqlTransportConnectionParams& params) {
        if (!mysql_handle || !m_conn_ctx) {
            if (m_conn_ctx) {
                m_conn_ctx->setErrorManually(MySqlTransportError::Category::InternalError, "OptionsSetter: Null MySQL handle or connection context.");
            }
            return false;
        }

        auto set_mysql_opt_ptr = [&](mysql_option option, const void* arg, const std::string& opt_name) -> bool {
            if (mysql_options(mysql_handle, option, arg) != 0) {
                m_conn_ctx->recordPreConnectOptionError("Failed to set " + opt_name);
                return false;
            }
            return true;
        };
        auto set_mysql_opt_uint = [&](mysql_option option, unsigned int arg, const std::string& opt_name) -> bool {
            if (mysql_options(mysql_handle, option, &arg) != 0) {  // Pass address of uint
                m_conn_ctx->recordPreConnectOptionError("Failed to set " + opt_name);
                return false;
            }
            return true;
        };
        auto set_mysql_opt_bool_as_char = [&](mysql_option option, bool val, const std::string& opt_name) -> bool {
            char char_val = val ? 1 : 0;
            if (mysql_options(mysql_handle, option, &char_val) != 0) {
                m_conn_ctx->recordPreConnectOptionError("Failed to set " + opt_name);
                return false;
            }
            return true;
        };

        if (params.connect_timeout_seconds.has_value()) {
            if (!set_mysql_opt_uint(MYSQL_OPT_CONNECT_TIMEOUT, params.connect_timeout_seconds.value(), "MYSQL_OPT_CONNECT_TIMEOUT")) return false;
        }
        if (params.read_timeout_seconds.has_value()) {
            if (!set_mysql_opt_uint(MYSQL_OPT_READ_TIMEOUT, params.read_timeout_seconds.value(), "MYSQL_OPT_READ_TIMEOUT")) return false;
        }
        if (params.write_timeout_seconds.has_value()) {
            if (!set_mysql_opt_uint(MYSQL_OPT_WRITE_TIMEOUT, params.write_timeout_seconds.value(), "MYSQL_OPT_WRITE_TIMEOUT")) return false;
        }

        auto ssl_mode_it = params.ssl_options.find("ssl_mode");
        if (ssl_mode_it != params.ssl_options.end()) {
            unsigned int ssl_mode_val = mapSslModeStringToValue(ssl_mode_it->second);
            // MYSQL_OPT_SSL_MODE expects unsigned int* as arg
            if (!set_mysql_opt_uint(MYSQL_OPT_SSL_MODE, ssl_mode_val, "MYSQL_OPT_SSL_MODE")) return false;
        }

        for (const auto& pair : params.ssl_options) {
            if (pair.first == "ssl_key") {
                if (!set_mysql_opt_ptr(MYSQL_OPT_SSL_KEY, pair.second.c_str(), "MYSQL_OPT_SSL_KEY")) return false;
            } else if (pair.first == "ssl_cert") {
                if (!set_mysql_opt_ptr(MYSQL_OPT_SSL_CERT, pair.second.c_str(), "MYSQL_OPT_SSL_CERT")) return false;
            } else if (pair.first == "ssl_ca") {
                if (!set_mysql_opt_ptr(MYSQL_OPT_SSL_CA, pair.second.c_str(), "MYSQL_OPT_SSL_CA")) return false;
            } else if (pair.first == "ssl_capath") {
                if (!set_mysql_opt_ptr(MYSQL_OPT_SSL_CAPATH, pair.second.c_str(), "MYSQL_OPT_SSL_CAPATH")) return false;
            } else if (pair.first == "ssl_cipher") {
                if (!set_mysql_opt_ptr(MYSQL_OPT_SSL_CIPHER, pair.second.c_str(), "MYSQL_OPT_SSL_CIPHER")) return false;
            } else if (pair.first == "ssl_mode") {
                continue;
            } else {
                // Log unknown SSL option or ignore
            }
        }

        for (const auto& pair : params.generic_options) {
            mysql_option opt_enum = pair.first;
            const std::string& opt_val_str = pair.second;

            if (opt_enum == MYSQL_INIT_COMMAND || opt_enum == MYSQL_SET_CHARSET_NAME || opt_enum == MYSQL_SET_CHARSET_DIR || opt_enum == MYSQL_PLUGIN_DIR ||  // Corrected from MYSQL_OPT_PLUGIN_DIR
                opt_enum == MYSQL_DEFAULT_AUTH || opt_enum == MYSQL_SERVER_PUBLIC_KEY ||                                                                      // Corrected from MYSQL_OPT_SERVER_PUBLIC_KEY
#if MYSQL_VERSION_ID >= 50607
                opt_enum == MYSQL_OPT_CONNECT_ATTR_RESET ||  // This option is fine
#endif
                false  // Add other const char* options here
            ) {
                if (!set_mysql_opt_ptr(opt_enum, opt_val_str.c_str(), "Generic char* option " + std::to_string(opt_enum))) return false;
            } else if (opt_enum == MYSQL_OPT_RECONNECT || opt_enum == MYSQL_ENABLE_CLEARTEXT_PLUGIN || opt_enum == MYSQL_OPT_CAN_HANDLE_EXPIRED_PASSWORDS || opt_enum == MYSQL_OPT_COMPRESS || opt_enum == MYSQL_OPT_LOCAL_INFILE) {
                bool val = (opt_val_str == "1" || opt_val_str == "true" || opt_val_str == "TRUE" || opt_val_str == "ON");
                if (!set_mysql_opt_bool_as_char(opt_enum, val, "Generic bool(char) option " + std::to_string(opt_enum))) return false;
            } else if (opt_enum == MYSQL_OPT_PROTOCOL || opt_enum == MYSQL_OPT_MAX_ALLOWED_PACKET || opt_enum == MYSQL_OPT_NET_BUFFER_LENGTH
                       // MYSQL_OPT_CONNECT_TIMEOUT, READ_TIMEOUT, WRITE_TIMEOUT are handled above
            ) {
                try {
                    unsigned int val_uint = std::stoul(opt_val_str);
                    if (!set_mysql_opt_uint(opt_enum, val_uint, "Generic uint option " + std::to_string(opt_enum))) return false;
                } catch (const std::exception&) {
                    m_conn_ctx->recordPreConnectOptionError("Invalid integer value '" + opt_val_str + "' for option " + std::to_string(opt_enum));
                    return false;
                }
            } else {
                if (!set_mysql_opt_ptr(opt_enum, opt_val_str.c_str(), "Generic unknown-type (assumed char*) option " + std::to_string(opt_enum))) return false;
            }
        }

        if (params.charset.has_value() && !params.charset.value().empty()) {
            bool charset_already_set_via_generic = params.generic_options.count(MYSQL_SET_CHARSET_NAME);
            if (!charset_already_set_via_generic) {
                if (!set_mysql_opt_ptr(MYSQL_SET_CHARSET_NAME, params.charset.value().c_str(), "MYSQL_SET_CHARSET_NAME")) return false;
            }
        }

        return true;
    }

}  // namespace cpporm_mysql_transport