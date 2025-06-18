#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"  // For transport_manager_ access

namespace neo4j_bolt_transport {

    // This helper prepares parameters for a BEGIN message.
    boltprotocol::BeginMessageParams AsyncSessionHandle::_prepare_begin_message_params(const std::optional<AsyncTransactionConfigOverrides>& tx_config) {
        boltprotocol::BeginMessageParams begin_p;

        if (!current_bookmarks_.empty()) {
            begin_p.bookmarks = current_bookmarks_;
        }

        if (stream_context_) {
            if (session_params_.database_name.has_value()) {
                begin_p.db = session_params_.database_name;
            }
            if (session_params_.impersonated_user.has_value()) {
                begin_p.imp_user = session_params_.impersonated_user;
            }
            // Access mode for BEGIN (Bolt 5.0+)
            if (stream_context_->negotiated_bolt_version >= boltprotocol::versions::V5_0) {
                if (session_params_.default_access_mode == config::AccessMode::READ) {
                    begin_p.mode = "r";
                }
                // Default is "w" (write) if not specified by session_params_ or overridden
            }

            if (tx_config.has_value()) {
                if (tx_config.value().metadata.has_value()) {
                    begin_p.tx_metadata = tx_config.value().metadata.value();
                }
                if (tx_config.value().timeout.has_value()) {
                    begin_p.tx_timeout = static_cast<int64_t>(tx_config.value().timeout.value().count());
                }
            } else if (transport_manager_ && transport_manager_->get_config().explicit_transaction_timeout_default_ms > 0) {
                begin_p.tx_timeout = static_cast<int64_t>(transport_manager_->get_config().explicit_transaction_timeout_default_ms);
            }
        }
        return begin_p;
    }

    // _prepare_run_message_params was previously in async_session_handle_query_execution.cpp or header.
    // If it's also heavily used by transaction logic (e.g. run_query_in_transaction_async preparing its own RUN),
    // it can live here. For now, assuming its primary definition is elsewhere or in the header if general.
    // If it's only used by run_query_in_transaction_async, it should be co-located with it or be a static helper.
    // For this refactoring, let's assume _prepare_run_message_params is defined in the header or its original location.
    // The one in async_session_handle.h for auto-commit queries is distinct.

}  // namespace neo4j_bolt_transport