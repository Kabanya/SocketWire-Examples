add_executable(packet-stream-client client.cpp)
add_executable(packet-stream-server server.cpp)

target_include_directories(packet-stream-server PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(packet-stream-client PRIVATE SocketWire)
target_link_libraries(packet-stream-server PRIVATE SocketWire)
