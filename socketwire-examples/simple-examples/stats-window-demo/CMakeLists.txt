add_executable(stats-window-demo-server server.cpp protocol.hpp)
add_executable(stats-window-demo-client client.cpp protocol.hpp)

target_include_directories(stats-window-demo-server PRIVATE ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(stats-window-demo-server PRIVATE SocketWire)
target_link_libraries(stats-window-demo-client PRIVATE SocketWire)
