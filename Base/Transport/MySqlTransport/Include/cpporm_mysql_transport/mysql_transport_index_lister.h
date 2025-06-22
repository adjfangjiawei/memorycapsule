#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_types.h"  // For MySqlTransportIndexInfo, MySqlTransportError

namespace cpporm_mysql_transport {

    class MySqlTransportConnection;  // Forward declaration

    class MySqlTransportIndexLister {
      public:
        explicit MySqlTransportIndexLister(MySqlTransportConnection* connection_context);

        std::optional<std::vector<MySqlTransportIndexInfo>> getTableIndexes(const std::string& table_name, const std::string& db_name_filter = "");
        std::optional<MySqlTransportIndexInfo> getPrimaryIndex(const std::string& table_name, const std::string& db_name_filter = "");

        MySqlTransportError getLastError() const;

      private:
        MySqlTransportConnection* m_conn_ctx;
        mutable MySqlTransportError m_last_error;

        void clearError_();
        void setError_(MySqlTransportError::Category cat, const std::string& msg);
        void setErrorFromConnection_(const std::string& context = "");
    };

}  // namespace cpporm_mysql_transport