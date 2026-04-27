add_executable(lobby-dots-client client.cpp)
add_executable(lobby-dots-lobby lobby.cpp)
add_executable(lobby-dots-game-server game_server.cpp)

target_include_directories(lobby-dots-client PUBLIC ${raylib_SOURCE_DIR}/include)
target_include_directories(lobby-dots-client PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)
target_include_directories(lobby-dots-lobby PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)
target_include_directories(lobby-dots-game-server PUBLIC ${raylib_SOURCE_DIR}/include)
target_include_directories(lobby-dots-game-server PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(lobby-dots-client PRIVATE raylib SocketWire)
target_link_libraries(lobby-dots-lobby PRIVATE SocketWire)
target_link_libraries(lobby-dots-game-server PRIVATE raylib SocketWire)
