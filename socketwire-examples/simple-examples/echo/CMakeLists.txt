add_executable(echo-server server.cpp)
add_executable(echo-client client.cpp)

target_link_libraries(echo-server PRIVATE SocketWire)
target_link_libraries(echo-client PRIVATE SocketWire)
