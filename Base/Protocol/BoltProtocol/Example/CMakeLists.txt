# CMakeLists.txt for BoltProtocol module examples

add_executable(client_example
    client_example_main.cpp
    client_example_utils.h
    client_example_utils.cpp
    client_example_session.h
    client_example_session.cpp
    # client_example_transaction.h # This header is now split
    # client_example_transaction.cpp # This source is now split
    client_example_tx_begin.h
    client_example_tx_begin.cpp
    client_example_tx_run.h
    client_example_tx_run.cpp
    client_example_tx_pull.h
    client_example_tx_pull.cpp
    client_example_tx_commit.h
    client_example_tx_commit.cpp
)
target_link_libraries(client_example PUBLIC BoltProtocol)

add_executable(server_example
    server_example_main.cpp
    server_example_utils.h
    server_example_utils.cpp
    server_example_handlers.h
    server_example_handlers.cpp
)
target_link_libraries(server_example PUBLIC BoltProtocol)