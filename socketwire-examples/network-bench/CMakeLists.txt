add_executable(netbench-socketwire-server server.cpp)
add_executable(netbench-socketwire-client client.cpp)

target_include_directories(netbench-socketwire-server PRIVATE
  ${CMAKE_SOURCE_DIR}/socketwire-examples/common)
target_include_directories(netbench-socketwire-client PRIVATE
  ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(netbench-socketwire-server PRIVATE SocketWire)
target_link_libraries(netbench-socketwire-client PRIVATE SocketWire)
