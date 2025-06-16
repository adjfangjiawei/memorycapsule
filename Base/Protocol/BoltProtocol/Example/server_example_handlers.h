#ifndef SERVER_EXAMPLE_HANDLERS_H
#define SERVER_EXAMPLE_HANDLERS_H

#include <map>
#include <memory>  // For std::shared_ptr
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/packstream_writer.h"
#include "server_example_utils.h"  // For error printing

namespace ServerHandlers {

    boltprotocol::BoltError handle_hello_message(const boltprotocol::HelloMessageParams& hello_params,  // Server doesn't usually parse HELLO into params, but receives a PSS.
                                                                                                        // For simplicity here, we'll assume it got the extra_auth_tokens map.
                                                                                                        // A more realistic handler would take PackStreamStructure.
                                                 boltprotocol::PackStreamWriter& response_writer);

    boltprotocol::BoltError handle_run_message(const boltprotocol::RunMessageParams& run_params, boltprotocol::PackStreamWriter& response_writer);

    // Helper for deserializing RunMessageParams from a PackStreamStructure
    // This is specific to how the server_example_main will provide the RUN message
    boltprotocol::BoltError deserialize_run_params_from_struct(const boltprotocol::PackStreamStructure& run_struct, boltprotocol::RunMessageParams& out_params);

}  // namespace ServerHandlers

#endif  // SERVER_EXAMPLE_HANDLERS_H