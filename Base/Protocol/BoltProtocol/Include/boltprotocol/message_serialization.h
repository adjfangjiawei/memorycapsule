#ifndef BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H
#define BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H

#include <optional>  // For optional return values or out-params
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"       // For message parameter structs, Value, BoltError, MessageTag
#include "boltprotocol/packstream_reader.h"  // For PackStreamReader
#include "boltprotocol/packstream_writer.h"  // For PackStreamWriter

namespace boltprotocol {

    // --- Client Message Serialization ---
    // These functions take message parameters, construct the appropriate PackStreamStructure,
    // and then serialize it using a PackStreamWriter into the provided buffer.

    /**
     * @brief Serializes a HELLO message.
     * @param params The parameters for the HELLO message.
     * @param writer The PackStreamWriter to use for serialization.
     * @return BoltError::SUCCESS on success, or an error code.
     */
    BoltError serialize_hello_message(const HelloMessageParams& params, PackStreamWriter& writer);

    /**
     * @brief Serializes a RUN message.
     * @param params The parameters for the RUN message.
     * @param writer The PackStreamWriter to use for serialization.
     * @return BoltError::SUCCESS on success, or an error code.
     */
    BoltError serialize_run_message(const RunMessageParams& params, PackStreamWriter& writer);

    /**
     * @brief Serializes a PULL message.
     * @param params The parameters for the PULL message.
     * @param writer The PackStreamWriter to use for serialization.
     * @return BoltError::SUCCESS on success, or an error code.
     */
    BoltError serialize_pull_message(const PullMessageParams& params, PackStreamWriter& writer);

    /**
     * @brief Serializes a DISCARD message.
     * @param params The parameters for the DISCARD message.
     * @param writer The PackStreamWriter to use for serialization.
     * @return BoltError::SUCCESS on success, or an error code.
     */
    BoltError serialize_discard_message(const DiscardMessageParams& params, PackStreamWriter& writer);

    /**
     * @brief Serializes a GOODBYE message (no parameters).
     * @param writer The PackStreamWriter to use for serialization.
     * @return BoltError::SUCCESS on success, or an error code.
     */
    BoltError serialize_goodbye_message(PackStreamWriter& writer);

    /**
     * @brief Serializes a RESET message (no parameters).
     * @param writer The PackStreamWriter to use for serialization.
     * @return BoltError::SUCCESS on success, or an error code.
     */
    BoltError serialize_reset_message(PackStreamWriter& writer);

    // TODO: Add serialization for BEGIN, COMMIT, ROLLBACK, ROUTE, TELEMETRY etc.

    // --- Server Message Deserialization ---
    // These functions take a PackStreamReader (which is assumed to be positioned at the start
    // of a PackStream structure representing a message), read the structure, validate its tag
    // and content, and populate the corresponding message parameter struct.

    /**
     * @brief Deserializes a SUCCESS message.
     * @param reader The PackStreamReader to use for deserialization.
     * @param out_params The structure to populate with deserialized parameters.
     * @return BoltError::SUCCESS on success, or an error code.
     *         The reader must be positioned at the start of the SUCCESS message structure.
     */
    BoltError deserialize_success_message(PackStreamReader& reader, SuccessMessageParams& out_params);

    /**
     * @brief Deserializes a FAILURE message.
     * @param reader The PackStreamReader to use for deserialization.
     * @param out_params The structure to populate with deserialized parameters.
     * @return BoltError::SUCCESS on success, or an error code.
     */
    BoltError deserialize_failure_message(PackStreamReader& reader, FailureMessageParams& out_params);

    /**
     * @brief Deserializes a RECORD message.
     * @param reader The PackStreamReader to use for deserialization.
     * @param out_params The structure to populate with deserialized parameters.
     * @return BoltError::SUCCESS on success, or an error code.
     */
    BoltError deserialize_record_message(PackStreamReader& reader, RecordMessageParams& out_params);

    /**
     * @brief Deserializes an IGNORED message.
     * @param reader The PackStreamReader to use for deserialization.
     * @return BoltError::SUCCESS on success if an IGNORED message is correctly parsed, or an error code.
     *         There are no parameters for IGNORED.
     */
    BoltError deserialize_ignored_message(PackStreamReader& reader);

    // TODO: Add deserialization for other server messages if needed by the client logic.

    // Helper to peek message tag (useful for dispatching deserialization)
    // This function attempts to read just the structure header to get the tag,
    // without consuming the entire message yet.
    // The reader's state might be affected (it will consume the header).
    // Alternatively, a more complex peeker could try to rewind or use a separate reader.
    // For now, assume it consumes the header.
    // A better approach might be:
    // 1. Reader reads a Value.
    // 2. Dispatcher checks if Value is a PSS.
    // 3. Dispatcher gets tag from PSS.
    // 4. Dispatcher calls appropriate deserialize_xxx(const PackStreamStructure&, XxxParams&).
    // This simplifies the deserialize_xxx functions.

    /**
     * @brief Attempts to read a PackStream structure and return its tag.
     * This is a utility function; the main deserialization functions will handle full structures.
     * @param reader The PackStreamReader to read from.
     * @param out_tag The message tag read from the structure.
     * @param out_fields_count The number of fields in the structure.
     * @return BoltError::SUCCESS if a structure header was read successfully.
     *         The reader will have consumed the structure header.
     */
    BoltError peek_message_structure_header(PackStreamReader& reader, uint8_t& out_tag, uint32_t& out_fields_count);

}  // namespace boltprotocol

#endif  // BOLT_PROTOCOL_IMPL_MESSAGE_SERIALIZATION_H