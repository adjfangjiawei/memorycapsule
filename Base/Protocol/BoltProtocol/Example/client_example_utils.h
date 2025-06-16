#ifndef CLIENT_EXAMPLE_UTILS_H
#define CLIENT_EXAMPLE_UTILS_H

#include <cstdint>    // For uint8_t
#include <exception>  // For std::bad_alloc, std::exception
#include <iomanip>    // For std::setw, std::setfill
#include <iostream>
#include <map>     // For std::map
#include <memory>  // For std::shared_ptr, std::make_shared
#include <sstream>
#include <string>
#include <vector>

#include "boltprotocol/chunking.h"
#include "boltprotocol/message_defs.h"
#include "boltprotocol/message_serialization.h"
#include "boltprotocol/packstream_reader.h"  // Needed for PackStreamReader in print_bolt_error_details_client
#include "boltprotocol/packstream_writer.h"

void print_bolt_error_details_client(
    const std::string& context, boltprotocol::BoltError err_code, boltprotocol::PackStreamReader* reader = nullptr, boltprotocol::PackStreamWriter* writer = nullptr, boltprotocol::ChunkedReader* chunk_reader = nullptr, boltprotocol::ChunkedWriter* chunk_writer = nullptr);

void print_bytes_client(const std::string& prefix, const std::vector<uint8_t>& bytes);

boltprotocol::BoltError send_and_receive_raw_message_client(
    std::stringstream& client_to_server_pipe, std::stringstream& server_to_client_pipe, const std::vector<uint8_t>& raw_message_to_send, std::vector<uint8_t>& out_raw_response_received, const std::string& message_description_for_log, bool expect_response = true);

boltprotocol::BoltError simulate_server_simple_success_response(std::stringstream& server_pipe, const std::string& context_log, int64_t qid = -1);

#endif  // CLIENT_EXAMPLE_UTILS_H