#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_types.h"  // For MySqlTransportError

namespace cpporm_mysql_transport {

    class MySqlTransportConnection;  // Forward declaration

    class MySqlTransportTableLister {
      public:
        explicit MySqlTransportTableLister(MySqlTransportConnection* connection_context);

        std::optional<std::vector<std::string>> listTables(const std::string& db_name_filter = "", const std::string& table_name_pattern = "");
        std::optional<std::vector<std::string>> listViews(const std::string& db_name_filter = "", const std::string& view_name_pattern = "");

        MySqlTransportError getLastError() const;

      private:
        MySqlTransportConnection* m_conn_ctx;
        mutable MySqlTransportError m_last_error;

        void clearError_();
        void setError_(MySqlTransportError::Category cat, const std::string& msg);
        void setErrorFromConnection_(const std::string& context = "");

        // Helper for common logic of SHOW FULL TABLES
        std::optional<std::vector<std::string>> listShowFullTablesFiltered(const std::string& db_name_filter,
                                                                           const std::string& name_pattern,
                                                                           const std::string& target_table_type  // "BASE TABLE" or "VIEW"
        );
    };

}  // namespace cpporm_mysql_transport