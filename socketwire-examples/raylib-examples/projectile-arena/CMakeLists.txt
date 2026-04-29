add_executable(projectile-arena-server server.cpp protocol.hpp)
add_executable(projectile-arena-client client.cpp protocol.hpp)

target_include_directories(projectile-arena-server PRIVATE ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(projectile-arena-server PRIVATE SocketWire)
target_link_libraries(projectile-arena-client PRIVATE SocketWire raylib)
