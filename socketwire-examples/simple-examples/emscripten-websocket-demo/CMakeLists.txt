if(EMSCRIPTEN)
  add_executable(emscripten-websocket-demo client.cpp)

  target_link_libraries(emscripten-websocket-demo PRIVATE SocketWire)
  target_link_options(emscripten-websocket-demo PRIVATE
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXIT_RUNTIME=0"
  )

  set_target_properties(emscripten-websocket-demo PROPERTIES SUFFIX ".html")
endif()
