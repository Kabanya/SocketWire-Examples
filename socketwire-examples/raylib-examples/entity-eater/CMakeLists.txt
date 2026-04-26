add_executable(entity-eater-client main.cpp protocol.cpp)
add_executable(entity-eater-server server.cpp protocol.cpp)

target_include_directories(entity-eater-client PUBLIC ${raylib_SOURCE_DIR}/include)
target_include_directories(entity-eater-client PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)
target_include_directories(entity-eater-server PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(entity-eater-client PRIVATE raylib SocketWire)
target_link_libraries(entity-eater-server PRIVATE SocketWire)
