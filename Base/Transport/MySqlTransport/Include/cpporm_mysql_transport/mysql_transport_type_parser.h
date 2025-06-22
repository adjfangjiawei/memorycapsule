// cpporm_mysql_transport/mysql_transport_type_parser.h
#pragma once

#include <string>
// #include "cpporm_mysql_transport/mysql_transport_types.h" // For MySqlTransportFieldMeta
// MySqlTransportFieldMeta is forward declared in the function signature instead

namespace cpporm_mysql_transport {
    // Forward declare MySqlTransportFieldMeta to avoid circular dependency or unnecessary include
    struct MySqlTransportFieldMeta;
    // Declaration of the parsing function
    bool parseMySQLTypeStringInternal(const std::string& type_str_orig, MySqlTransportFieldMeta& field_meta);
}  // namespace cpporm_mysql_transport