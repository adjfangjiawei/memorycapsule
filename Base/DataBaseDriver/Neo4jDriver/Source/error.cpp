#include "neo4j_bolt_driver/error.h"

#include <sstream>  // For Error::to_string

namespace neo4j_bolt_driver {

    // Error Constructors
    Error::Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {
    }

    Error::Error(ErrorCode c, std::string msg, std::string s_code) : code(c), message(std::move(msg)), server_code(std::move(s_code)) {
    }

    Error::Error(ErrorCode c, std::string msg, int sys_errno) : code(c), message(std::move(msg)), system_errno(sys_errno) {
    }

    Error::Error(ErrorCode c, std::string msg, std::string s_code, int sys_errno) : code(c), message(std::move(msg)), server_code(std::move(s_code)), system_errno(sys_errno) {
    }

    std::string Error::to_string() const {
        std::ostringstream oss;
        // A simple way to get a string representation of the enum
        // In a real C++26 project, you might use a library for enum to string or std::format if available for enums
        oss << "Error Code: " << static_cast<int>(code);  // Placeholder, better to have actual string names
        oss << ", Message: \"" << message << "\"";
        if (server_code) {
            oss << ", ServerCode: \"" << *server_code << "\"";
        }
        if (system_errno) {
            oss << ", SystemErrno: " << *system_errno;
        }
        return oss.str();
    }

    // Comparison operators
    bool operator==(const Error& lhs, const Error& rhs) {
        // Define equality based on what makes sense for your error handling.
        // Typically, the error code is primary. Message might be too variable for strict equality.
        // Server code and system errno might also be important for specific equality checks.
        return lhs.code == rhs.code && lhs.message == rhs.message &&  // Or decide if message should be part of equality
               lhs.server_code == rhs.server_code && lhs.system_errno == rhs.system_errno;
    }

    bool operator!=(const Error& lhs, const Error& rhs) {
        return !(lhs == rhs);
    }

}  // namespace neo4j_bolt_driver