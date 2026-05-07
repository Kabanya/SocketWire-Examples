include(${PROJECT_SOURCE_DIR}/cmake/common.cmake)

foreach(example
  entity-eater
  lobby-dots
  prediction-ships
  ship-swarm
  cipher-ships
  projectile-arena
)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${example}/CMakeLists.txt")
    add_subdirectory(${example})
  endif()
endforeach()
