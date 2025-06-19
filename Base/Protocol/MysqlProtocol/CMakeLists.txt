file(GLOB_RECURSE MYSQL_PROTOCOL_SOURCES
    CONFIGURE_DEPENDS
    "Source/*.cpp"
    "Source/**/*.cpp"
)

add_library(MysqlProtocol ${MYSQL_PROTOCOL_SOURCES})

target_include_directories(MysqlProtocol
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

target_link_libraries(MysqlProtocol PUBLIC Boost::asio OpenSSL::SSL OpenSSL::Crypto unofficial::libmysql::libmysql)
# build example, no condition
# add_subdirectory(Example)