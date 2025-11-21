add_executable(math-duel-client client.cpp client.hpp)
add_executable(math-duel-server server.cpp server.hpp)

target_link_libraries(echo-server PRIVATE SocketWire)
target_link_libraries(echo-client PRIVATE SocketWire)
target_link_libraries(math-duel-client PRIVATE SocketWire)
target_link_libraries(math-duel-server PRIVATE SocketWire)