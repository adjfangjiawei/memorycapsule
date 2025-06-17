#ifndef NEO4J_BOLT_TRANSPORT_SESSION_HANDLE_H
#define NEO4J_BOLT_TRANSPORT_SESSION_HANDLE_H

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "config/session_parameters.h"
#include "internal/bolt_physical_connection.h"
#include "neo4j_transaction_work.h"  // <<< ADDED FOR TRANSACTION FUNCTIONS
#include "result_stream.h"

namespace neo4j_bolt_transport {

    class Neo4jBoltTransport;

    class SessionHandle {
      public:
        SessionHandle(Neo4jBoltTransport* transport_manager, internal::BoltPhysicalConnection::PooledConnection connection, config::SessionParameters params);
        ~SessionHandle();

        SessionHandle(const SessionHandle&) = delete;
        SessionHandle& operator=(const SessionHandle&) = delete;
        SessionHandle(SessionHandle&& other) noexcept;
        SessionHandle& operator=(SessionHandle&& other) noexcept;

        // --- Explicit Transaction Management ---
        std::pair<boltprotocol::BoltError, std::string> begin_transaction(  // Returns error + message
            const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata = std::nullopt,
            const std::optional<std::chrono::milliseconds>& tx_timeout = std::nullopt);
        std::pair<boltprotocol::BoltError, std::string> commit_transaction();
        std::pair<boltprotocol::BoltError, std::string> rollback_transaction();
        bool is_in_transaction() const {
            return in_explicit_transaction_;
        }

        // --- Managed Transaction Functions ---
        // Executes work within a read transaction with automatic retries for transient errors.
        TransactionWorkResult execute_read_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata = std::nullopt, const std::optional<std::chrono::milliseconds>& tx_timeout = std::nullopt);
        // Executes work within a write transaction with automatic retries for transient errors.
        TransactionWorkResult execute_write_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata = std::nullopt, const std::optional<std::chrono::milliseconds>& tx_timeout = std::nullopt);

        // --- Query Execution ---
        // Returns a pair: {Error, ErrorMsg} and the stream. Stream is null on error.
        std::pair<std::pair<boltprotocol::BoltError, std::string>, std::unique_ptr<BoltResultStream>> run_query(const std::string& cypher,
                                                                                                                const std::map<std::string, boltprotocol::Value>& parameters = {},
                                                                                                                const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override = std::nullopt);

        // Returns {Error, ErrorMsg}, out_summary, out_failure
        std::pair<boltprotocol::BoltError, std::string> run_consume(const std::string& cypher,
                                                                    const std::map<std::string, boltprotocol::Value>& parameters,
                                                                    boltprotocol::SuccessMessageParams& out_summary,
                                                                    boltprotocol::FailureMessageParams& out_failure,
                                                                    const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata_override = std::nullopt);

        const std::vector<std::string>& get_last_bookmarks() const;
        void update_bookmarks(const std::vector<std::string>& new_bookmarks);

        void close();
        bool is_closed() const {
            return is_closed_;
        }
        bool is_connection_valid() const {
            return connection_is_valid_;
        }

        friend class BoltResultStream;

      private:
        std::pair<boltprotocol::BoltError, std::string> _prepare_auto_commit_stream(
            const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, boltprotocol::SuccessMessageParams& out_run_summary, boltprotocol::FailureMessageParams& out_failure);

        std::pair<boltprotocol::BoltError, std::string> _prepare_explicit_tx_stream(const std::string& cypher, const std::map<std::string, boltprotocol::Value>& parameters, boltprotocol::SuccessMessageParams& out_run_success, boltprotocol::FailureMessageParams& out_failure);

        std::pair<boltprotocol::BoltError, std::string> _stream_pull_records(std::optional<int64_t> qid, int64_t n, std::vector<boltprotocol::RecordMessageParams>& out_records, boltprotocol::SuccessMessageParams& out_pull_summary);
        std::pair<boltprotocol::BoltError, std::string> _stream_discard_records(std::optional<int64_t> qid, int64_t n, boltprotocol::SuccessMessageParams& out_discard_summary);

        void _release_connection_to_pool(bool mark_healthy = true);
        void _invalidate_session_due_to_connection_error(boltprotocol::BoltError error, const std::string& context_message);
        // Returns raw pointer, validity MUST be checked by caller. Sets out_err_pair.
        internal::BoltPhysicalConnection* _get_valid_connection_for_operation(std::pair<boltprotocol::BoltError, std::string>& out_err_pair, const std::string& operation_context);

        // Helper for managed transactions
        TransactionWorkResult _execute_transaction_work_internal(TransactionWork work,
                                                                 config::AccessMode mode,  // To inform BEGIN if this is the first attempt
                                                                 const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata,
                                                                 const std::optional<std::chrono::milliseconds>& tx_timeout);

        Neo4jBoltTransport* transport_manager_;
        internal::BoltPhysicalConnection::PooledConnection connection_;
        config::SessionParameters session_params_;

        bool in_explicit_transaction_ = false;
        std::optional<int64_t> current_transaction_query_id_;

        std::vector<std::string> current_bookmarks_;
        bool is_closed_ = false;
        bool connection_is_valid_ = true;
    };

}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_SESSION_HANDLE_H