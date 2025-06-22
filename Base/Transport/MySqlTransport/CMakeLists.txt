file(GLOB_RECURSE MYSQL_TRANSPORT_SOURCE
    CONFIGURE_DEPENDS
    "Source/*.cpp"
    "Source/**/*.cpp"
)

add_library(MySqlTransport ${MYSQL_TRANSPORT_SOURCE})

target_include_directories(MySqlTransport
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

target_link_libraries(MySqlTransport PUBLIC Qt6::Core MysqlProtocol)

# # build example, no condition
# add_subdirectory(Example)
