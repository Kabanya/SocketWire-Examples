add_executable(echo-server echo-server.cpp)
add_executable(echo-client echo-client.cpp)

target_link_libraries(echo-server PRIVATE SocketWire)
target_link_libraries(echo-client PRIVATE SocketWire)
