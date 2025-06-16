#ifndef SERVER_EXAMPLE_HANDLERS_H
#define SERVER_EXAMPLE_HANDLERS_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/packstream_writer.h"
#include "server_example_utils.h"

namespace ServerHandlers {

    boltprotocol::BoltError handle_hello_message(const boltprotocol::HelloMessageParams& parsed_hello_params, boltprotocol::PackStreamWriter& response_writer, const boltprotocol::versions::Version& server_negotiated_version);

    boltprotocol::BoltError handle_run_message(const boltprotocol::RunMessageParams& run_params,  // Now receives fully parsed params
                                               boltprotocol::PackStreamWriter& response_writer
                                               // const boltprotocol::versions::Version& server_negotiated_version // Optional: if needed
    );

    // Removed deserialize_run_params_from_struct as its functionality is now
    // part of deserialize_run_message_request from the core library.

}  // namespace ServerHandlers

#endif  // SERVER_EXAMPLE_HANDLERS_H