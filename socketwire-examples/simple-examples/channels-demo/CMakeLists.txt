add_executable(channels-demo-server server.cpp protocol.hpp)
add_executable(channels-demo-client client.cpp protocol.hpp)

target_include_directories(channels-demo-server PRIVATE ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(channels-demo-server PRIVATE SocketWire)
target_link_libraries(channels-demo-client PRIVATE SocketWire)
