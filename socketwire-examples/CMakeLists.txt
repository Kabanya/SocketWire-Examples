add_subdirectory(simple-examples)

if(NOT EMSCRIPTEN)
  add_subdirectory(raylib-examples)
  add_subdirectory(network-bench)
endif()
