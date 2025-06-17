#ifndef NEO4J_BOLT_TRANSPORT_URI_URI_PARSER_H
#define NEO4J_BOLT_TRANSPORT_URI_URI_PARSER_H

#include <string>

#include "boltprotocol/bolt_errors_versions.h"  // For BoltError
#include "parsed_uri.h"

namespace neo4j_bolt_transport {
    namespace uri {

        class UriParser {
          public:
            UriParser() = delete;  // Static methods only

            // Parses the given URI string and populates the ParsedUri struct.
            // Returns BoltError::SUCCESS on success, or an error code if parsing fails.
            static boltprotocol::BoltError parse(const std::string& uri_string, ParsedUri& out_parsed_uri);
        };

    }  // namespace uri
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_URI_URI_PARSER_H