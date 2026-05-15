add_executable(persistent-arena-server server.cpp protocol.hpp)
add_executable(persistent-arena-client client.cpp protocol.hpp)

target_include_directories(persistent-arena-server PRIVATE ${CMAKE_SOURCE_DIR}/socketwire-examples/common)
target_include_directories(persistent-arena-client PRIVATE ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(persistent-arena-server PRIVATE SocketWire)
target_link_libraries(persistent-arena-client PRIVATE SocketWire raylib)
