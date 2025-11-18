cmake_minimum_required(VERSION 3.28)

add_executable(bitstream-sample bitstream-sample.cpp)

target_link_libraries(bitstream-sample PRIVATE SocketWire)