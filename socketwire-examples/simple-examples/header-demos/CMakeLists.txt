add_executable(bitstream-demo bitstream-demo.cpp)
add_executable(constants-demo constants-demo.cpp)

target_link_libraries(bitstream-demo PRIVATE SocketWire)
target_link_libraries(constants-demo PRIVATE SocketWire)