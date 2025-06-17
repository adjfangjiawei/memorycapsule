add_subdirectory(BoltProtocol)

add_library(Protocol INTERFACE)
target_link_libraries(Protocol INTERFACE BoltProtocol)