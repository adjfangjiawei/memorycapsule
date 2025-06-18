#include <utility>  // For std::move

#include "neo4j_bolt_transport/neo4j_transaction_context.h"  // For TransactionWork typedef
#include "neo4j_bolt_transport/session_handle.h"

namespace neo4j_bolt_transport {

    TransactionWorkResult SessionHandle::execute_read_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        // Note: The mode_hint (AccessMode::READ) is passed to _execute_transaction_work_internal.
        // That internal function will temporarily set session_params_.default_access_mode
        // for the duration of the managed transaction.
        return _execute_transaction_work_internal(std::move(work), config::AccessMode::READ, tx_metadata, tx_timeout);
    }

    TransactionWorkResult SessionHandle::execute_write_transaction(TransactionWork work, const std::optional<std::map<std::string, boltprotocol::Value>>& tx_metadata, const std::optional<std::chrono::milliseconds>& tx_timeout) {
        return _execute_transaction_work_internal(std::move(work), config::AccessMode::WRITE, tx_metadata, tx_timeout);
    }

}  // namespace neo4j_bolt_transport