#ifndef SERVER_EXAMPLE_UTILS_H
#define SERVER_EXAMPLE_UTILS_H

#include <cstdint>  // For uint8_t
#include <iomanip>  // For std::setw, std::setfill
#include <iostream>
#include <string>
#include <vector>

#include "boltprotocol/message_defs.h"
#include "boltprotocol/packstream_reader.h"
#include "boltprotocol/packstream_writer.h"

void print_bolt_error_details_server(const std::string& context, boltprotocol::BoltError err, boltprotocol::PackStreamReader* reader = nullptr, boltprotocol::PackStreamWriter* writer = nullptr);

void print_bytes_server(const std::string& prefix, const std::vector<uint8_t>& bytes);

#endif  // SERVER_EXAMPLE_UTILS_H