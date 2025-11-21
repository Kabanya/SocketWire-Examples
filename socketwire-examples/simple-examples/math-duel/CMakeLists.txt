add_executable(math-duel-client math-duel-client.cpp math-duel-client.hpp)
add_executable(math-duel-server math-duel-server.cpp math-duel-server.hpp)

target_link_libraries(echo-server PRIVATE SocketWire)
target_link_libraries(echo-client PRIVATE SocketWire)
target_link_libraries(math-duel-client PRIVATE SocketWire)
target_link_libraries(math-duel-server PRIVATE SocketWire)