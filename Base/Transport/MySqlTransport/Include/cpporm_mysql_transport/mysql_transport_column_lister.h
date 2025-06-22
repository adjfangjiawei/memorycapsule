#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_types.h"  // For MySqlTransportFieldMeta, MySqlTransportError

namespace cpporm_mysql_transport {

    class MySqlTransportConnection;  // Forward declaration

    class MySqlTransportColumnLister {
      public:
        explicit MySqlTransportColumnLister(MySqlTransportConnection* connection_context);

        std::optional<std::vector<MySqlTransportFieldMeta>> getTableColumns(const std::string& table_name, const std::string& db_name_filter = "");

        MySqlTransportError getLastError() const;

      private:
        MySqlTransportConnection* m_conn_ctx;
        mutable MySqlTransportError m_last_error;

        void clearError_();
        void setError_(MySqlTransportError::Category cat, const std::string& msg);
        void setErrorFromConnection_(const std::string& context = "");

        // Helper to parse the 'Type' string from SHOW COLUMNS output
        // This is a complex function and a key part of this lister.
        bool parseMySQLTypeString(const std::string& type_str, MySqlTransportFieldMeta& field_meta_to_update) const;
    };

}  // namespace cpporm_mysql_transport