// SqlDriver/Source/mysql/mysql_param_converter.cpp
#include "sqldriver/mysql/mysql_driver_helper.h"
// #include <mysql/mysql.h> // Not needed for param conversion logic itself
#include "cpporm_mysql_transport/mysql_transport_types.h"  // For MySqlTransportConnectionParams definition

namespace cpporm_sqldriver {
    namespace mysql_helper {

        // Use fully qualified name for transport params type
        ::cpporm_mysql_transport::MySqlTransportConnectionParams toMySqlTransportParams(const ConnectionParameters& params) {
            ::cpporm_mysql_transport::MySqlTransportConnectionParams transport_params;
            bool conv_ok_flag = true;

            auto get_opt_string = [&](const std::string& key) -> std::optional<std::string> {
                auto it = params.find(key);
                if (it != params.end() && !it->second.isNull()) {
                    bool ok_local = false;
                    std::string val = it->second.toString(&ok_local);
                    if (!ok_local) conv_ok_flag = false;
                    return val;
                }
                return std::nullopt;
            };
            // Removed unused get_opt_int
            auto get_opt_uint = [&](const std::string& key) -> std::optional<unsigned int> {
                auto it = params.find(key);
                if (it != params.end() && !it->second.isNull()) {
                    bool ok_local = false;
                    // SqlValue toUInt32 should be used for unsigned int
                    unsigned int val = it->second.toUInt32(&ok_local);
                    if (!ok_local) conv_ok_flag = false;
                    return val;
                }
                return std::nullopt;
            };

            transport_params.host = params.hostName().value_or("localhost");
            transport_params.port = static_cast<unsigned int>(params.port().value_or(3306));
            transport_params.user = params.userName().value_or("");
            transport_params.password = params.password().value_or("");
            transport_params.db_name = params.dbName().value_or("");

            if (auto val = get_opt_string(ConnectionParameters::KEY_CLIENT_CHARSET)) transport_params.charset = val;
            // Corrected key name:
            if (auto val = get_opt_uint(ConnectionParameters::KEY_CONNECTION_TIMEOUT_SECONDS)) transport_params.connect_timeout_seconds = val;
            if (auto val = get_opt_uint(ConnectionParameters::KEY_READ_TIMEOUT_SECONDS)) transport_params.read_timeout_seconds = val;
            if (auto val = get_opt_uint(ConnectionParameters::KEY_WRITE_TIMEOUT_SECONDS)) transport_params.write_timeout_seconds = val;

            if (auto val = get_opt_string(ConnectionParameters::KEY_SSL_MODE)) transport_params.ssl_options["ssl_mode"] = *val;
            if (auto val = get_opt_string(ConnectionParameters::KEY_SSL_KEY_PATH)) transport_params.ssl_options["ssl_key"] = *val;
            if (auto val = get_opt_string(ConnectionParameters::KEY_SSL_CERT_PATH)) transport_params.ssl_options["ssl_cert"] = *val;
            if (auto val = get_opt_string(ConnectionParameters::KEY_SSL_CA_PATH)) transport_params.ssl_options["ssl_ca"] = *val;
            if (auto val = get_opt_string(ConnectionParameters::KEY_SSL_CIPHER)) transport_params.ssl_options["ssl_cipher"] = *val;

            // Handling for generic_options and init_commands needs a defined convention in ConnectionParameters
            // Example: iterate all params and check for a prefix like "mysql.option.<mysql_option_enum_as_string>"
            // or "mysql.init_command.<command_key>"
            // This part is complex due to mapping string keys to mysql_option enums and string values to void* or specific types.
            // For simplicity, this is often handled by dedicated setters in ConnectionParameters or a more elaborate parsing mechanism.
            // The current MySqlTransportConnectionParams expects generic_options as std::map<mysql_option, std::string>
            // and init_commands as std::map<std::string, std::string>.
            // A simple pass-through of ConnectionParameters::KEY_CONNECT_OPTIONS string is not directly usable.

            if (auto conn_opts_str = get_opt_string(ConnectionParameters::KEY_CONNECT_OPTIONS)) {
                // Here you would parse conn_opts_str, which could be like "CLIENT_FOUND_ROWS=1;MYSQL_OPT_RECONNECT=true"
                // And populate transport_params.client_flag or transport_params.generic_options accordingly.
                // This parsing is non-trivial and omitted for brevity.
                // Example of setting client_flag if it was a simple integer option:
                // if (conn_opts_str->find("SOME_CLIENT_FLAG_NAME") != std::string::npos) {
                //    transport_params.client_flag |= SOME_MYSQL_CLIENT_FLAG_MACRO;
                // }
            }

            if (!conv_ok_flag) {
                // Optionally log a warning here that some parameters might not have converted correctly.
            }

            return transport_params;
        }

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver