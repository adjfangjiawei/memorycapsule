file(GLOB_RECURSE SQL_DRIVER_SOURCE
    CONFIGURE_DEPENDS
    "Source/*.cpp"
    "Source/**/*.cpp"
)

add_library(SqlDriver ${SQL_DRIVER_SOURCE})

target_include_directories(SqlDriver
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Include"
)

target_link_libraries(SqlDriver PUBLIC Qt6::Core MySqlTransport)

# # build example, no condition
# add_subdirectory(Example)
