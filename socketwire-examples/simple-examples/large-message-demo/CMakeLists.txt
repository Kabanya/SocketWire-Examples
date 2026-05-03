add_executable(large-message-demo-server server.cpp protocol.hpp)
add_executable(large-message-demo-client client.cpp protocol.hpp)

target_include_directories(large-message-demo-server PRIVATE ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(large-message-demo-server PRIVATE SocketWire)
target_link_libraries(large-message-demo-client PRIVATE SocketWire)
