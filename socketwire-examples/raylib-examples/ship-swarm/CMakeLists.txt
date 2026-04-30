add_executable(ship-swarm-client main.cpp protocol.cpp entity.cpp)
add_executable(ship-swarm-server server.cpp protocol.cpp entity.cpp)

target_include_directories(ship-swarm-client PUBLIC ${raylib_SOURCE_DIR}/include)
target_include_directories(ship-swarm-client PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)
target_include_directories(ship-swarm-server PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(ship-swarm-client PRIVATE raylib SocketWire)
target_link_libraries(ship-swarm-server PRIVATE SocketWire)
