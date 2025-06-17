file(GLOB_RECURSE NEO4J_BOLT_DRIVER_SOURCES
    CONFIGURE_DEPENDS
    "Source/*.cpp"
    "Source/**/*.cpp"
)

add_library(Neo4jBoltDriver ${NEO4J_BOLT_DRIVER_SOURCES})

target_include_directories(Neo4jBoltDriver
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

target_link_libraries(Neo4jBoltDriver PUBLIC Boost::asio Boost::system)
target_link_libraries(Neo4jBoltDriver PUBLIC Protocol)