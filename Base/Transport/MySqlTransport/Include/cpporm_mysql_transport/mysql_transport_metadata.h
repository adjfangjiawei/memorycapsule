#pragma once

#include "cpporm_mysql_transport/mysql_transport_types.h"
// Forward declarations for PImpl or direct inclusion of helper headers
#include <memory>  // For std::unique_ptr
#include <optional>
#include <string>
#include <vector>

#include "cpporm_mysql_transport/mysql_transport_column_lister.h"
#include "cpporm_mysql_transport/mysql_transport_database_lister.h"
#include "cpporm_mysql_transport/mysql_transport_index_lister.h"
#include "cpporm_mysql_transport/mysql_transport_table_lister.h"

namespace cpporm_mysql_transport {

    class MySqlTransportConnection;  // Forward declaration

    class MySqlTransportMetadata {
      public:
        explicit MySqlTransportMetadata(MySqlTransportConnection* conn);
        ~MySqlTransportMetadata();  // Required for unique_ptr to incomplete types if defined in .cpp

        // Movable (if unique_ptrs are handled correctly)
        MySqlTransportMetadata(MySqlTransportMetadata&& other) noexcept;
        MySqlTransportMetadata& operator=(MySqlTransportMetadata&& other) noexcept;
        // Non-copyable
        MySqlTransportMetadata(const MySqlTransportMetadata&) = delete;
        MySqlTransportMetadata& operator=(const MySqlTransportMetadata&) = delete;

        // Delegated methods
        std::optional<std::vector<std::string>> listDatabases(const std::string& db_name_pattern = "");
        std::optional<std::vector<std::string>> listTables(const std::string& db_name = "", const std::string& table_name_pattern = "");
        std::optional<std::vector<std::string>> listViews(const std::string& db_name = "", const std::string& view_name_pattern = "");
        std::optional<std::vector<MySqlTransportFieldMeta>> getTableColumns(const std::string& table_name, const std::string& db_name = "");
        std::optional<std::vector<MySqlTransportIndexInfo>> getTableIndexes(const std::string& table_name, const std::string& db_name = "");
        std::optional<MySqlTransportIndexInfo> getPrimaryIndex(const std::string& table_name, const std::string& db_name = "");

        MySqlTransportError getLastError() const;

      private:
        // MySqlTransportConnection* m_connection_ctx; // No longer needed if all ops are via listers that store it
        mutable MySqlTransportError m_last_error_aggregator;  // Aggregates error from the last lister call

        std::unique_ptr<MySqlTransportDatabaseLister> m_db_lister;
        std::unique_ptr<MySqlTransportTableLister> m_table_lister;
        std::unique_ptr<MySqlTransportColumnLister> m_column_lister;
        std::unique_ptr<MySqlTransportIndexLister> m_index_lister;

        void clearError();                                // Clears m_last_error_aggregator
        void setError(const MySqlTransportError& error);  // Sets m_last_error_aggregator

        // Template helper to call a lister and update aggregated error
        template <typename ListerPtr, typename Method, typename... Args>
        auto callLister(ListerPtr& lister_ptr, Method method, const std::string& error_context, Args&&... args) -> decltype(((*lister_ptr).*method)(std::forward<Args>(args)...));
    };

}  // namespace cpporm_mysql_transport