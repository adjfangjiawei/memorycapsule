#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_types.h"  // For MySqlTransportError

namespace cpporm_mysql_transport {

    class MySqlTransportConnection;  // Forward declaration

    class MySqlTransportDatabaseLister {
      public:
        explicit MySqlTransportDatabaseLister(MySqlTransportConnection* connection_context);

        std::optional<std::vector<std::string>> listDatabases(const std::string& db_name_pattern = "");

        MySqlTransportError getLastError() const;

      private:
        MySqlTransportConnection* m_conn_ctx;  // For executing queries and accessing connection utilities
        mutable MySqlTransportError m_last_error;

        void clearError_();  // Suffix to avoid conflict if MySqlTransportMetadata also has one
        void setError_(MySqlTransportError::Category cat, const std::string& msg);
        void setErrorFromConnection_(const std::string& context = "");
    };

}  // namespace cpporm_mysql_transport