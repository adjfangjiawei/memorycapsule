#ifndef BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H
#define BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"  // For message parameter structs, Value, BoltError, MessageTag
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"

// Forward declare the Version struct if only passed by const ref and not constructed here.
// However, serialize_route_message takes it, so message_defs.h (which defines it) is fine.
// namespace boltprotocol { namespace versions { struct Version; } }

namespace boltprotocol {

    // --- Client Message Serialization (Client -> Server) ---

    BoltError serialize_hello_message(const HelloMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_run_message(const RunMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_pull_message(const PullMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_discard_message(const DiscardMessageParams& params, PackStreamWriter& writer);
    BoltError serialize_goodbye_message(PackStreamWriter& writer);
    BoltError serialize_reset_message(PackStreamWriter& writer);

    // Transaction messages
    BoltError serialize_begin_message(const BeginMessageParams& params, PackStreamWriter& writer);  // ADDED DECLARATION
    BoltError serialize_commit_message(PackStreamWriter& writer);                                   // ADDED DECLARATION
    BoltError serialize_rollback_message(PackStreamWriter& writer);                                 // ADDED DECLARATION

    // Routing and Telemetry messages
    // Note: serialize_route_message takes the negotiated Bolt version to adapt its structure.
    BoltError serialize_route_message(const RouteMessageParams& params, PackStreamWriter& writer, const versions::Version& negotiated_bolt_version);  // ADDED DECLARATION
    // TODO: BoltError serialize_telemetry_message(const TelemetryMessageParams& params, PackStreamWriter& writer);

    // --- Server Message Deserialization (Server -> Client) ---

    BoltError deserialize_success_message(PackStreamReader& reader, SuccessMessageParams& out_params);
    BoltError deserialize_failure_message(PackStreamReader& reader, FailureMessageParams& out_params);
    BoltError deserialize_record_message(PackStreamReader& reader, RecordMessageParams& out_params);
    BoltError deserialize_ignored_message(PackStreamReader& reader);

    BoltError peek_message_structure_header(PackStreamReader& reader, uint8_t& out_tag, uint32_t& out_fields_count);

}  // namespace boltprotocol

#endif  // BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H