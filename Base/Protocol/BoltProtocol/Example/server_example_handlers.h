#ifndef SERVER_EXAMPLE_HANDLERS_H
#define SERVER_EXAMPLE_HANDLERS_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"  // Includes versions::Version via bolt_errors_versions.h
#include "boltprotocol/packstream_writer.h"
#include "server_example_utils.h"

namespace ServerHandlers {

    boltprotocol::BoltError handle_hello_message(const boltprotocol::HelloMessageParams& parsed_hello_params, boltprotocol::PackStreamWriter& response_writer,
                                                 const boltprotocol::versions::Version& server_negotiated_version);  // Added version

    boltprotocol::BoltError handle_run_message(const boltprotocol::RunMessageParams& run_params, boltprotocol::PackStreamWriter& response_writer);

    boltprotocol::BoltError deserialize_run_params_from_struct(const boltprotocol::PackStreamStructure& run_struct, boltprotocol::RunMessageParams& out_params);

}  // namespace ServerHandlers

#endif  // SERVER_EXAMPLE_HANDLERS_H