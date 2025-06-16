# CMakeLists.txt for BoltProtocol module examples

# client_example has been refactored
add_executable(client_example
    client_example_main.cpp
    client_example_utils.h
    client_example_utils.cpp
    client_example_session.h
    client_example_session.cpp
    client_example_transaction.h
    client_example_transaction.cpp
)
target_link_libraries(client_example PUBLIC BoltProtocol)

# server_example has been refactored
add_executable(server_example
    server_example_main.cpp
    server_example_utils.h
    server_example_utils.cpp
    server_example_handlers.h
    server_example_handlers.cpp
)
target_link_libraries(server_example PUBLIC BoltProtocol)