add_executable(cipher-ships-client main.cpp protocol.cpp entity.cpp)
add_executable(cipher-ships-server server.cpp protocol.cpp entity.cpp)

target_include_directories(cipher-ships-client PUBLIC ${raylib_SOURCE_DIR}/include)
target_include_directories(cipher-ships-client PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)
target_include_directories(cipher-ships-server PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(cipher-ships-client PRIVATE raylib SocketWire)
target_link_libraries(cipher-ships-server PRIVATE SocketWire)
