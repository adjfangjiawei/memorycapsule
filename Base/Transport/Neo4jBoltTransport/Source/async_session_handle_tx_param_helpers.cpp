#include "neo4j_bolt_transport/async_session_handle.h"
#include "neo4j_bolt_transport/neo4j_bolt_transport.h"

namespace neo4j_bolt_transport {

    // This helper prepares parameters for a BEGIN message.
    boltprotocol::BeginMessageParams AsyncSessionHandle::_prepare_begin_message_params(const std::optional<AsyncTransactionConfigOverrides>& tx_config) {
        boltprotocol::BeginMessageParams begin_p;

        if (!current_bookmarks_.empty()) {  // Use current session bookmarks
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
            // Assuming V5_0 is a defined macro in boltprotocol::versions
            if (stream_context_->negotiated_bolt_version >= boltprotocol::versions::V5_0) {
                if (session_params_.default_access_mode == config::AccessMode::READ) {
                    begin_p.mode = "r";
                }
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

    // _prepare_run_message_params is now moved to/defined in async_session_handle_query_execution.cpp
    // as it's used by both auto-commit and explicit-tx run methods which are in that file or
    // async_session_handle_explicit_tx_query.cpp respectively.
    // The version in async_session_handle.h now has the bool is_in_explicit_tx parameter.

}  // namespace neo4j_bolt_transport