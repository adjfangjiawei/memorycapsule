#ifndef CLIENT_EXAMPLE_SESSION_H
#define CLIENT_EXAMPLE_SESSION_H

#include <array>  // For handshake
#include <sstream>
#include <string>
#include <vector>

#include "boltprotocol/handshake.h"
#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"
#include "client_example_utils.h"  // For helpers

struct ClientSession {
    std::stringstream client_to_server_stream;
    std::stringstream server_to_client_stream;
    boltprotocol::versions::Version negotiated_version;
    boltprotocol::BoltError last_error = boltprotocol::BoltError::SUCCESS;

    ClientSession() = default;

    boltprotocol::BoltError perform_handshake_sequence();
    boltprotocol::BoltError send_hello_sequence();
    boltprotocol::BoltError send_goodbye_sequence();
};

#endif  // CLIENT_EXAMPLE_SESSION_H