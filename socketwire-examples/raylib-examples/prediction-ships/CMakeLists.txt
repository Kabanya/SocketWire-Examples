add_executable(prediction-ships-client main.cpp protocol.cpp entity.cpp)
add_executable(prediction-ships-server server.cpp protocol.cpp entity.cpp)

target_include_directories(prediction-ships-client PUBLIC ${raylib_SOURCE_DIR}/include)
target_include_directories(prediction-ships-client PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)
target_include_directories(prediction-ships-server PUBLIC ${CMAKE_SOURCE_DIR}/socketwire-examples/common)

target_link_libraries(prediction-ships-client PRIVATE raylib SocketWire)
target_link_libraries(prediction-ships-server PRIVATE SocketWire)
