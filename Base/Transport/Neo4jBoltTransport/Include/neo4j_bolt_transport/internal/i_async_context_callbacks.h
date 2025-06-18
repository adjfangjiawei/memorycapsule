#ifndef NEO4J_BOLT_TRANSPORT_INTERNAL_I_ASYNC_CONTEXT_CALLBACKS_H
#define NEO4J_BOLT_TRANSPORT_INTERNAL_I_ASYNC_CONTEXT_CALLBACKS_H

#include <cstdint>  // For uint64_t
#include <memory>
#include <string>

#include "boltprotocol/bolt_errors_versions.h"  // For BoltError
#include "spdlog/fwd.h"                         // Forward declaration for spdlog::logger

namespace neo4j_bolt_transport {
    namespace internal {

        class IAsyncContextCallbacks {
          public:
            virtual ~IAsyncContextCallbacks() = default;

            virtual std::shared_ptr<spdlog::logger> get_logger() const = 0;
            virtual uint64_t get_id_for_logging() const = 0;
            virtual void mark_as_defunct_from_async(boltprotocol::BoltError reason, const std::string& message) = 0;
            virtual boltprotocol::BoltError get_last_error_code_from_async() const = 0;
        };

    }  // namespace internal
}  // namespace neo4j_bolt_transport

#endif  // NEO4J_BOLT_TRANSPORT_INTERNAL_I_ASYNC_CONTEXT_CALLBACKS_H