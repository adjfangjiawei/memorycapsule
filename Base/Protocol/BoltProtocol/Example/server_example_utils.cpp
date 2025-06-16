#include "server_example_utils.h"

void print_bolt_error_details_server(const std::string& context, boltprotocol::BoltError err, boltprotocol::PackStreamReader* reader, boltprotocol::PackStreamWriter* writer) {
    std::cerr << "Error (Server) " << context << ": " << static_cast<int>(err);
    if (reader && reader->has_error() && reader->get_error() != err) {
        std::cerr << " (Reader specific error: " << static_cast<int>(reader->get_error()) << ")";
    }
    if (writer && writer->has_error() && writer->get_error() != err) {
        std::cerr << " (Writer specific error: " << static_cast<int>(writer->get_error()) << ")";
    }
    std::cerr << std::endl;
}

void print_bytes_server(const std::string& prefix, const std::vector<uint8_t>& bytes) {
    std::cout << prefix;
    if (bytes.empty()) {
        std::cout << "(empty)" << std::endl;
        return;
    }
    for (uint8_t byte : bytes) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
    }
    std::cout << std::dec << " (size: " << bytes.size() << ")" << std::endl;
}