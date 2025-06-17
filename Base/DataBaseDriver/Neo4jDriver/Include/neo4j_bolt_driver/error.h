#ifndef NEO4J_BOLT_DRIVER_ERROR_H
#define NEO4J_BOLT_DRIVER_ERROR_H

#include <optional>  // For optional server_code and system_errno
#include <string>
#include <vector>  // For potential list of underlying errors in the future

namespace neo4j_bolt_driver {

    // Enumeration of possible error codes/categories
    enum class ErrorCode {
        // Generic Errors
        Unknown,
        OperationFailed,

        // Connection Errors
        NetworkUnreachable,
        ConnectionRefused,
        ConnectionTimeout,
        ConnectionReadTimeout,
        ConnectionWriteTimeout,
        ConnectionClosedByPeer,
        TLSHandshakeFailed,
        DNSResolutionFailed,
        AddressResolutionFailed,  // More general than DNS

        // Protocol Errors
        BoltHandshakeFailed,
        BoltUnsupportedVersion,
        BoltUnexpectedMessage,
        BoltInvalidMessageFormat,
        BoltMaxConnectionsReached,  // Example if server indicates this

        // PackStream Errors
        PackStreamSerializationError,
        PackStreamDeserializationError,
        PackStreamUnexpectedType,
        PackStreamBufferOverflow,
        PackStreamNotEnoughData,
        PackStreamIntegerOutOfRange,
        PackStreamStringTooLong,

        // Authentication Errors
        AuthenticationFailed,
        CredentialsExpired,
        AuthorizationFailed,

        // Database Errors (from server FAILURE messages)
        DatabaseError,  // Generic database error from server
        DatabaseSyntaxError,
        DatabaseConstraintViolation,
        DatabaseTransientError,  // e.g., leader switch, deadlock, suggest retry
        DatabaseClientError,     // Errors classified by Neo4j as client-side issues
        DatabaseUnavailable,     // Server indicates it's temporarily unavailable

        // Driver Internal Errors
        DriverInternalError,
        FeatureNotImplemented,
        InvalidArgument,
        InvalidState,  // Driver is in a state not allowing the operation
        ConfigurationError,
        ResourceAllocationFailed
    };

    // Structure to hold detailed error information for std::expected<T, Error>
    struct Error {
        ErrorCode code = ErrorCode::Unknown;
        std::string message;
        std::optional<std::string> server_code;  // e.g., "Neo.ClientError.Statement.SyntaxError"
        std::optional<int> system_errno;         // Underlying OS error code (from socket, file ops etc.)
        // std::vector<Error> underlying_errors; // Future: for cascading errors

        // Constructors
        Error() = default;

        Error(ErrorCode c, std::string msg);

        Error(ErrorCode c, std::string msg, std::string s_code);

        Error(ErrorCode c, std::string msg, int sys_errno);

        Error(ErrorCode c, std::string msg, std::string s_code, int sys_errno);

        // For debugging or logging
        std::string to_string() const;
    };

    // Comparison operators for Error (primarily for testing or specific logic)
    // Based on code and optionally message for more specific checks if needed.
    bool operator==(const Error& lhs, const Error& rhs);
    bool operator!=(const Error& lhs, const Error& rhs);

}  // namespace neo4j_bolt_driver

#endif  // NEO4J_BOLT_DRIVER_ERROR_H