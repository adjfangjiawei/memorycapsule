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

    BoltError serialize_hello_message(const HelloMessageParams& params, PackStreamWriter& writer);
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
    BoltError serialize_telemetry_message(const TelemetryMessageParams& params, PackStreamWriter& writer);  // <--- ADDED DECLARATION

    // --- Server Message Deserialization (Server -> Client) ---

    BoltError deserialize_success_message(PackStreamReader& reader, SuccessMessageParams& out_params);
    BoltError deserialize_failure_message(PackStreamReader& reader, FailureMessageParams& out_params);
    BoltError deserialize_record_message(PackStreamReader& reader, RecordMessageParams& out_params);
    BoltError deserialize_ignored_message(PackStreamReader& reader);

    BoltError deserialize_message_structure_prelude(PackStreamReader& reader, MessageTag expected_tag, size_t expected_fields_min, size_t expected_fields_max, PackStreamStructure& out_structure_contents);

    BoltError peek_message_structure_header(PackStreamReader& reader, uint8_t& out_tag, uint32_t& out_fields_count);

}  // namespace boltprotocol

#endif  // BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H