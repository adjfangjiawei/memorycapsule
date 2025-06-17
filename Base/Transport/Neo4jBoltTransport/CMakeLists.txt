file(GLOB_RECURSE NEO4J_BOLT_TRANSPORT_SOURCE
    CONFIGURE_DEPENDS
    "Source/*.cpp"
    "Source/**/*.cpp"
)

add_library(Neo4jBoltTransport ${NEO4J_BOLT_TRANSPORT_SOURCE})

target_include_directories(Neo4jBoltTransport
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

target_link_libraries(Neo4jBoltTransport PUBLIC Boost::asio Boost::system)
target_link_libraries(Neo4jBoltTransport PUBLIC Protocol)
target_compile_definitions(Neo4jBoltTransport PUBLIC SPDLOG_USE_STD_FORMAT)
target_link_libraries(Neo4jBoltTransport
    PUBLIC
    spdlog::spdlog_header_only
)