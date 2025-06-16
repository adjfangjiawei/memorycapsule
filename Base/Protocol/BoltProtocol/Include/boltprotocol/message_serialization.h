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

    // Note: The `negotiated_bolt_version` parameter might be more accurately named
    // `target_bolt_version_for_hello` or similar, as HELLO is sent before full negotiation response.
    // Client sends HELLO based on its capabilities and what it expects/supports from server.
    // For simplicity, we'll keep using negotiated_bolt_version, assuming client has a target.
    BoltError serialize_hello_message(const HelloMessageParams& params, PackStreamWriter& writer, const versions::Version& client_target_version);  // Added client_target_version
    BoltError serialize_run_message(const RunMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_pull_message(const PullMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_discard_message(const DiscardMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_goodbye_message(PackStreamWriter& writer);
    BoltError serialize_reset_message(PackStreamWriter& writer);

    // Transaction messages
    BoltError serialize_begin_message(const BeginMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_commit_message(PackStreamWriter& writer);
    BoltError serialize_rollback_message(PackStreamWriter& writer);

    // Routing and Telemetry messages
    BoltError serialize_route_message(const RouteMessageParams& params, PackStreamWriter& writer, const versions::Version& negotiated_bolt_version);
    BoltError serialize_telemetry_message(const TelemetryMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_logon_message(const LogonMessageParams& params, PackStreamWriter& writer);  // Added
    BoltError serialize_logoff_message(PackStreamWriter& writer);                                   // Added (LogoffMessageParams is empty)

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
    BoltError deserialize_hello_message_request(PackStreamReader& reader, HelloMessageParams& out_params, const versions::Version& server_negotiated_version);  // Added server_negotiated_version
    BoltError deserialize_run_message_request(PackStreamReader& reader, RunMessageParams& out_params);
    // TODO: BoltError deserialize_logon_message_request(PackStreamReader& reader, LogonMessageParams& out_params);
    // TODO: BoltError deserialize_logoff_message_request(PackStreamReader& reader); // LOGOFF has no params

}  // namespace boltprotocol

#endif  // BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H