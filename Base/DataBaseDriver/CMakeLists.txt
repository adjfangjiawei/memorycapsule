# add_subdirectory(Neo4jBoltDriver)
add_subdirectory(SqlDriver)

add_library(DataBaseDriver INTERFACE)
target_link_libraries(DataBaseDriver INTERFACE SqlDriver)