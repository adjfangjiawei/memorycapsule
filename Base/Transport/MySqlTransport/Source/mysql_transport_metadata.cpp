#include "cpporm_mysql_transport/mysql_transport_metadata.h"

#include "cpporm_mysql_transport/mysql_transport_connection.h"  // Listers might need it

// Listers are included via mysql_transport_metadata.h

namespace cpporm_mysql_transport {

    MySqlTransportMetadata::MySqlTransportMetadata(MySqlTransportConnection* conn) {
        if (!conn) {
            // This is a critical programming error.
            // Set an error that can be retrieved by getLastError(), or throw.
            m_last_error_aggregator = MySqlTransportError(MySqlTransportError::Category::InternalError, "MySqlTransportMetadata: Null connection context provided during construction.");
            // Listers will not be initialized. Calls to them will fail.
            return;
        }
        m_db_lister = std::make_unique<MySqlTransportDatabaseLister>(conn);
        m_table_lister = std::make_unique<MySqlTransportTableLister>(conn);
        m_column_lister = std::make_unique<MySqlTransportColumnLister>(conn);
        m_index_lister = std::make_unique<MySqlTransportIndexLister>(conn);
    }

    MySqlTransportMetadata::~MySqlTransportMetadata() = default;  // For std::unique_ptr to incomplete types

    // Move constructor
    MySqlTransportMetadata::MySqlTransportMetadata(MySqlTransportMetadata&& other) noexcept
        : m_last_error_aggregator(std::move(other.m_last_error_aggregator)), m_db_lister(std::move(other.m_db_lister)), m_table_lister(std::move(other.m_table_lister)), m_column_lister(std::move(other.m_column_lister)), m_index_lister(std::move(other.m_index_lister)) {
        // After moving unique_ptrs, 'other' lister pointers are null.
        // The listers themselves, if they store MySqlTransportConnection*, still point to the original connection.
        // This is generally fine if the MySqlTransportConnection outlives this MySqlTransportMetadata object,
        // or if the connection context in listers is updated (but they are simple pointers).
        // The new MySqlTransportMetadata object now owns the listers.
    }

    // Move assignment
    MySqlTransportMetadata& MySqlTransportMetadata::operator=(MySqlTransportMetadata&& other) noexcept {
        if (this != &other) {
            m_last_error_aggregator = std::move(other.m_last_error_aggregator);
            m_db_lister = std::move(other.m_db_lister);
            m_table_lister = std::move(other.m_table_lister);
            m_column_lister = std::move(other.m_column_lister);
            m_index_lister = std::move(other.m_index_lister);
        }
        return *this;
    }

    void MySqlTransportMetadata::clearError() {
        m_last_error_aggregator = MySqlTransportError();
    }

    void MySqlTransportMetadata::setError(const MySqlTransportError& error) {
        m_last_error_aggregator = error;
    }

    // Template helper implementation
    template <typename ListerPtr, typename Method, typename... Args>
    auto MySqlTransportMetadata::callLister(ListerPtr& lister_ptr, Method method, const std::string& error_context, Args&&... args) -> decltype(((*lister_ptr).*method)(std::forward<Args>(args)...)) {
        clearError();  // Clear aggregator before call
        using ReturnType = decltype(((*lister_ptr).*method)(std::forward<Args>(args)...));

        if (!lister_ptr) {
            setError(MySqlTransportError(MySqlTransportError::Category::InternalError, error_context + ": Lister component not initialized."));
            if constexpr (std::is_same_v<ReturnType, void>)
                return;
            else
                return ReturnType{};  // Return default-constructed optional or empty vector
        }

        ReturnType result = ((*lister_ptr).*method)(std::forward<Args>(args)...);
        m_last_error_aggregator = lister_ptr->getLastError();  // Get specific error from the lister

        // If lister operation failed (indicated by !result for optionals, or error set),
        // and lister itself didn't set a good error message, enhance it.
        bool result_indicates_failure = false;
        if constexpr (std::is_same_v<decltype(result), std::optional<typename ReturnType::value_type>>) {  // Check if ReturnType is std::optional
            if (!result.has_value()) result_indicates_failure = true;
        }
        // Add similar checks if result is vector and empty could mean failure in some contexts.

        if (result_indicates_failure && m_last_error_aggregator.isOk()) {
            setError(MySqlTransportError(MySqlTransportError::Category::InternalError, error_context + ": Operation failed but lister reported no specific error."));
        } else if (!m_last_error_aggregator.isOk() && m_last_error_aggregator.message.find(error_context) == std::string::npos) {
            // Prepend context to lister's error message if not already there
            m_last_error_aggregator.message = error_context + ": " + m_last_error_aggregator.message;
        }
        return result;
    }

    std::optional<std::vector<std::string>> MySqlTransportMetadata::listDatabases(const std::string& db_name_pattern) {
        return callLister(m_db_lister, &MySqlTransportDatabaseLister::listDatabases, "ListDatabases", db_name_pattern);
    }

    std::optional<std::vector<std::string>> MySqlTransportMetadata::listTables(const std::string& db_name, const std::string& table_name_pattern) {
        return callLister(m_table_lister, &MySqlTransportTableLister::listTables, "ListTables", db_name, table_name_pattern);
    }

    std::optional<std::vector<std::string>> MySqlTransportMetadata::listViews(const std::string& db_name, const std::string& view_name_pattern) {
        return callLister(m_table_lister, &MySqlTransportTableLister::listViews, "ListViews", db_name, view_name_pattern);
    }

    std::optional<std::vector<MySqlTransportFieldMeta>> MySqlTransportMetadata::getTableColumns(const std::string& table_name, const std::string& db_name) {
        return callLister(m_column_lister, &MySqlTransportColumnLister::getTableColumns, "GetTableColumns", table_name, db_name);
    }

    std::optional<std::vector<MySqlTransportIndexInfo>> MySqlTransportMetadata::getTableIndexes(const std::string& table_name, const std::string& db_name) {
        return callLister(m_index_lister, &MySqlTransportIndexLister::getTableIndexes, "GetTableIndexes", table_name, db_name);
    }

    std::optional<MySqlTransportIndexInfo> MySqlTransportMetadata::getPrimaryIndex(const std::string& table_name, const std::string& db_name) {
        return callLister(m_index_lister, &MySqlTransportIndexLister::getPrimaryIndex, "GetPrimaryIndex", table_name, db_name);
    }

    MySqlTransportError MySqlTransportMetadata::getLastError() const {
        return m_last_error_aggregator;
    }

}  // namespace cpporm_mysql_transport