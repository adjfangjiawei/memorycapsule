#ifndef BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H
#define BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"

namespace boltprotocol {

    // --- Client Message Serialization (Client -> Server) ---
    BoltError serialize_hello_message(const HelloMessageParams& params, PackStreamWriter& writer, const versions::Version& client_target_version);
    BoltError serialize_run_message(const RunMessageParams& params, PackStreamWriter& writer, const versions::Version& target_bolt_version);
    BoltError serialize_pull_message(const PullMessageParams& params, PackStreamWriter& writer);        // Takes PullMessageParams
    BoltError serialize_discard_message(const DiscardMessageParams& params, PackStreamWriter& writer);  // Takes DiscardMessageParams
    BoltError serialize_goodbye_message(PackStreamWriter& writer);
    BoltError serialize_reset_message(PackStreamWriter& writer);
    BoltError serialize_begin_message(const BeginMessageParams& params, PackStreamWriter& writer, const versions::Version& target_bolt_version);
    BoltError serialize_commit_message(PackStreamWriter& writer);
    BoltError serialize_rollback_message(PackStreamWriter& writer);
    BoltError serialize_route_message(const RouteMessageParams& params, PackStreamWriter& writer, const versions::Version& negotiated_bolt_version);
    BoltError serialize_telemetry_message(const TelemetryMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_logon_message(const LogonMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_logoff_message(PackStreamWriter& writer);

    // --- Server Message Deserialization (Server -> Client) ---
    // (These are for server sending responses to client)
    BoltError deserialize_success_message(PackStreamReader& reader, SuccessMessageParams& out_params);
    BoltError deserialize_failure_message(PackStreamReader& reader, FailureMessageParams& out_params);
    BoltError deserialize_record_message(PackStreamReader& reader, RecordMessageParams& out_params);
    BoltError deserialize_ignored_message(PackStreamReader& reader);

    BoltError deserialize_message_structure_prelude(PackStreamReader& reader, MessageTag expected_tag, size_t expected_fields_min, size_t expected_fields_max, PackStreamStructure& out_structure_contents);

    BoltError peek_message_structure_header(PackStreamReader& reader, uint8_t& out_tag, uint32_t& out_fields_count);

    // --- Server-Side Deserialization of Client Requests ---
    // (These are for server parsing requests from client)
    BoltError deserialize_hello_message_request(PackStreamReader& reader, HelloMessageParams& out_params, const versions::Version& server_negotiated_version);
    BoltError deserialize_run_message_request(PackStreamReader& reader, RunMessageParams& out_params, const versions::Version& server_negotiated_version);
    BoltError deserialize_logon_message_request(PackStreamReader& reader, LogonMessageParams& out_params);
    BoltError deserialize_logoff_message_request(PackStreamReader& reader);  // No out_params as LogoffMessageParams is empty
    BoltError deserialize_begin_message_request(PackStreamReader& reader, BeginMessageParams& out_params, const versions::Version& server_negotiated_version);

    BoltError deserialize_pull_message_request(PackStreamReader& reader, PullMessageParams& out_params);        // <--- ADDED
    BoltError deserialize_discard_message_request(PackStreamReader& reader, DiscardMessageParams& out_params);  // <--- ADDED
    BoltError deserialize_commit_message_request(PackStreamReader& reader);                                     // <--- ADDED (CommitMessageParams is empty)
    BoltError deserialize_rollback_message_request(PackStreamReader& reader);                                   // <--- ADDED (RollbackMessageParams is empty)
    BoltError deserialize_reset_message_request(PackStreamReader& reader);                                      // <--- ADDED (No params struct for RESET)
    BoltError deserialize_goodbye_message_request(PackStreamReader& reader);                                    // <--- ADDED (No params struct for GOODBYE)
    // TODO: Add deserializers for ROUTE, TELEMETRY requests if server needs to parse their specific params.

}  // namespace boltprotocol

#endif  // BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H