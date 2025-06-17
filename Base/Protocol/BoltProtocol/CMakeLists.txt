# CMakeLists.txt for BoltProtocol module

# Since this module is purely for protocol logic and data structures,
# it might not have complex dependencies beyond standard C++.
# If PackStream or other parts need external libs (e.g., for specific data types),
# they would be added here.

file(GLOB_RECURSE BOLT_PROTOCOL_SOURCES
    CONFIGURE_DEPENDS
    "Source/*.cpp"
    "Source/**/*.cpp"
)

add_library(BoltProtocol ${BOLT_PROTOCOL_SOURCES})

target_include_directories(BoltProtocol
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

target_link_libraries(BoltProtocol PUBLIC Boost::asio OpenSSL::SSL OpenSSL::Crypto)

# build example, no condition
add_subdirectory(Example)

# BoltProtocol itself likely doesn't link to socket libraries.
# It provides structures and serialization/deserialization logic.
# The Neo4jBoltDriver will link the socket library and use BoltProtocol.

# Example of how other modules would link against BoltProtocol:
# target_link_libraries(Neo4jBoltDriver PRIVATE BoltProtocol)