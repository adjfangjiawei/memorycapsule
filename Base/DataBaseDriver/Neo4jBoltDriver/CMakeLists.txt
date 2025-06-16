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

# Neo4jBoltDriver 依赖网络库。
# 请在此处或更高层级的 CMakeLists.txt 文件中链接您的网络库。
# 例如，如果您选择使用 Boost.Asio 且已通过 find_package(Boost) 找到:
# target_link_libraries(Neo4jBoltDriver PUBLIC Boost::asio Boost::system)
# 或者，如果您使用 Winsock:
# if(WIN32)
#   target_link_libraries(Neo4jBoltDriver PUBLIC ws2_32)
# endif()
# 如果是其他库，请相应修改。