cmake_minimum_required(VERSION 3.28)

add_executable(echo-server MinimalServer.cpp)
add_executable(echo-client MinimalClient.cpp)

target_link_libraries(echo-server PRIVATE SocketWire)
target_link_libraries(echo-client PRIVATE SocketWire)
