#ifndef NEO4J_BOLT_TRANSPORT_SESSION_HANDLE_H
#define NEO4J_BOLT_TRANSPORT_SESSION_HANDLE_H

#include <chrono>
#include <deque>
#include <map>  // For parameters
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "config/session_parameters.h"
#include "internal/bolt_physical_connection.h"
#include "neo4j_transaction_work.h"
#include "result_stream.h"  // Includes ResultSummary transitively

namespace neo4j_bolt_transport {

    class Neo4jBoltTransport;  // Forward declaration

    class SessionHandle {
      public:
        SessionHandle(Neo4jBoltTransport* transport_manager, internal::BoltPhysicalConnection::PooledConnection connection, config::SessionParameters params);
        ~SessionHandle();

        SessionHandle(const SessionHandle&) = delete;
        SessionHandle& operator=(const SessionHandle&) = delete;
        SessionHandle(SessionHandle&& other) noexcept;
        SessionHandle& operator=(SessionHandle&& other) noexcept;

        // --- Explicit Transaction Management ---
        std::pair<boltprotocol::BoltError, std::string> begin_transaction(const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata = std::nullopt, const std::optional<std::chrono::milliseconds>& tx_timeout = std::nullopt);
        std::pair<boltprotocol::BoltError, std::string> commit_transaction();
        std::pair<boltprotocol::BoltError, std::string> rollback_transaction();
        bool is_in_transaction() const {
            return in_explicit_transaction_;
        }

        // --- Managed Transaction Functions ---
        TransactionWorkResult execute_read_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata = std::nullopt, const std::optional<std::chrono::milliseconds>& tx_timeout = std::nullopt);
        TransactionWorkResult execute_write_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata = std::nullopt, const std::optional<std::chrono::milliseconds>& tx_timeout = std::nullopt);

        // --- Query Execution ---
        std::pair<std::pair<boltprotocol::BoltError, std::string>, std::unique_ptr<BoltResultStream>> run_query(const std::string& cypher,
                                                                                                                const std::map<std::string, boltprotocol::Value>& parameters = {},
                                                                                                                const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override = std::nullopt);

        // Consumes result, returns final summary.
        std::pair<std::pair<boltprotocol::BoltError, std::string>, ResultSummary> run_query_and_consume(  // <--- MODIFIED to return typed ResultSummary
            const std::string& cypher,
            const std::map<std::string, boltprotocol::Value>& parameters,
            const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override = std::nullopt);

        // Convenience: runs query and consumes, returns only error status
        std::pair<boltprotocol::BoltError, std::string> run_query_without_result(  // <--- NEW
            const std::string& cypher,
            const std::map<std::string, boltprotocol::Value>& parameters = {},
            const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override = std::nullopt);

        const std::vector<std::string>& get_last_bookmarks() const;
        void update_bookmarks(const std::vector<std::string>& new_bookmarks);  // Renamed from set_last_bookmarks for clarity

        void close();
        bool is_closed() const {
            return is_closed_;
        }
        bool is_connection_valid() const {
            return connection_is_valid_;
        }  // Changed from connection_healthy_

        // Allow ResultStream to access internal methods for fetching/discarding
        friend class BoltResultStream;
        friend class TransactionContext;  // To call run_query from within a transaction function

      private:
        std::pair<boltprotocol::BoltError, std::string> _prepare_auto_commit_run(  // Renamed from _prepare_auto_commit_stream
            const std::string& cypher,
            const std::map<std::string, boltprotocol::Value>& parameters,
            const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata,
            boltprotocol::SuccessMessageParams& out_run_summary_raw,  // Raw params
            boltprotocol::FailureMessageParams& out_failure_details_raw);

        std::pair<boltprotocol::BoltError, std::string> _prepare_explicit_tx_run(  // Renamed
            const std::string& cypher,
            const std::map<std::string, boltprotocol::Value>& parameters,
            boltprotocol::SuccessMessageParams& out_run_summary_raw,
            boltprotocol::FailureMessageParams& out_failure_details_raw);

        // These interact with BoltPhysicalConnection
        std::pair<boltprotocol::BoltError, std::string> _stream_pull_records(std::optional<int64_t> qid, int64_t n, std::vector<boltprotocol::RecordMessageParams>& out_records, boltprotocol::SuccessMessageParams& out_pull_summary_raw);
        std::pair<boltprotocol::BoltError, std::string> _stream_discard_records(std::optional<int64_t> qid, int64_t n, boltprotocol::SuccessMessageParams& out_discard_summary_raw);

        void _release_connection_to_pool(bool mark_healthy = true);
        void _invalidate_session_due_to_connection_error(boltprotocol::BoltError error, const std::string& context_message);
        internal::BoltPhysicalConnection* _get_valid_connection_for_operation(std::pair<boltprotocol::BoltError, std::string>& out_err_pair, const std::string& operation_context);

        TransactionWorkResult _execute_transaction_work_internal(TransactionWork work, config::AccessMode mode, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout);

        Neo4jBoltTransport* transport_manager_;  // Owning transport system
        internal::BoltPhysicalConnection::PooledConnection connection_;
        config::SessionParameters session_params_;

        bool in_explicit_transaction_ = false;
        std::optional<int64_t> current_transaction_query_id_;  // For Bolt < 4.0 or when qid is returned by RUN in TX

        std::vector<std::string> current_bookmarks_;
        bool is_closed_ = false;
        bool connection_is_valid_ = true;
    };

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_SESSION_HANDLE_H