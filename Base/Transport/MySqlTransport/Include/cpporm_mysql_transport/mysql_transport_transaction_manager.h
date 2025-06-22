// cpporm_mysql_transport/mysql_transport_transaction_manager.h
#pragma once

#include <optional>
#include <string>

#include "cpporm_mysql_transport/mysql_transport_types.h"  // For TransactionIsolationLevel, MySqlTransportError

namespace cpporm_mysql_transport {

    class MySqlTransportConnection;  // Forward declaration for context

    class MySqlTransportTransactionManager {
      public:
        explicit MySqlTransportTransactionManager(MySqlTransportConnection* connection_context);

        bool beginTransaction();
        bool commit();
        bool rollback();
        bool setTransactionIsolation(TransactionIsolationLevel level);
        std::optional<TransactionIsolationLevel> getTransactionIsolation() const;  // May query server
        bool setSavepoint(const std::string& name);
        bool rollbackToSavepoint(const std::string& name);
        bool releaseSavepoint(const std::string& name);

        // This method is to update the cached isolation level if changed externally or on new connection
        void updateCachedIsolationLevel(TransactionIsolationLevel level);

      private:
        friend class MySqlTransportConnection;               // Added friend declaration
        MySqlTransportConnection* m_conn_ctx;                // Pointer to the parent connection
        TransactionIsolationLevel m_cached_isolation_level;  // Cache to avoid frequent server queries

        // Helper to execute simple queries via the connection context
        bool executeSimpleQueryOnConnection(const std::string& query, const std::string& context_message);
    };

}  // namespace cpporm_mysql_transport