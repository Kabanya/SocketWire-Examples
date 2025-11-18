cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable C++ module scanning to avoid problems because of cmake 3.28
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(socketwire LANGUAGES CXX)

# comment if you want have a structure in build folder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Prevent the user from polluting the root directory with build files
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif()

# For enabling debug stuff in "dev" builds, which are RelWithDebInfo.
if (NOT CMAKE_BUILD_TYPE STREQUAL Release)
  set(SOCKETWIRE_DEBUG ON)
else ()
  set(SOCKETWIRE_DEBUG OFF)
endif ()

# No profiling in release builds, mostly because Tracy emmits warnings in that case.
option(TRACY_ENABLE "Enable profiling" ${SOCKETWIRE_DEBUG})

# Uncomment to contribute into SocketWire (clone SocketWire to the same folder)
# set(CPM_socketwire_SOURCE "${PROJECT_SOURCE_DIR}/../SocketWire")

include("cmake/get_cpm.cmake")
include("cmake/thirdparty.cmake")
include("cmake/get_version.cmake")

add_subdirectory(socketwire-examples)
