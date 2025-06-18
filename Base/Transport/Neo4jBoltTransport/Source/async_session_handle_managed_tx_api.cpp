#include <utility>  // For std::move

#include "neo4j_bolt_transport/async_session_handle.h"

namespace neo4j_bolt_transport {

    boost::asio::awaitable<TransactionWorkResult> AsyncSessionHandle::execute_read_transaction_async(AsyncTransactionWork work, const std::optional<AsyncTransactionConfigOverrides>& tx_config) {
        co_return co_await _execute_transaction_work_internal_async(std::move(work), config::AccessMode::READ, tx_config);
    }

    boost::asio::awaitable<TransactionWorkResult> AsyncSessionHandle::execute_write_transaction_async(AsyncTransactionWork work, const std::optional<AsyncTransactionConfigOverrides>& tx_config) {
        co_return co_await _execute_transaction_work_internal_async(std::move(work), config::AccessMode::WRITE, tx_config);
    }

}  // namespace neo4j_bolt_transport