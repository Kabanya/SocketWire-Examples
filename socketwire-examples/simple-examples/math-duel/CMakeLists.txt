cmake_minimum_required(VERSION 3.28)

add_executable(math-duel-client ClientTestSocket.cpp ClientTestSocket.h)
add_executable(math-duel-server ServerTestSocket.cpp ServerTestSocket.h)

target_link_libraries(echo-server PRIVATE SocketWire)
target_link_libraries(echo-client PRIVATE SocketWire)
target_link_libraries(math-duel-client PRIVATE SocketWire)
target_link_libraries(math-duel-server PRIVATE SocketWire)