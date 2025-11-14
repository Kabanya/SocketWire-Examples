cmake_minimum_required(VERSION 3.28)

# Simple logging without headaches
CPMAddPackage(
  NAME spdlog
  GITHUB_REPOSITORY gabime/spdlog
  VERSION 1.15.3
  OPTIONS
    SPDLOG_USE_STD_FORMAT ON  # std::format from C++20
)

# A profiler for both CPU and GPU
CPMAddPackage(
  GITHUB_REPOSITORY wolfpld/tracy
  GIT_TAG v0.12.2
  OPTIONS
    "TRACY_ON_DEMAND ON"
)

# core library here - SocketWire
CPMAddPackage(
  NAME socketwire
  GITHUB_REPOSITORY egzha-dev/SocketWire
  GIT_TAG main
)

# Popular cross-platform library for graphics and game development
CPMAddPackage(
  NAME raylib
  GITHUB_REPOSITORY raysan5/raylib
  GIT_TAG 5.5
)

# Reliable UDP networking library
CPMAddPackage(
  NAME enet
  GITHUB_REPOSITORY lsalzman/enet
  GIT_TAG v1.3.18
)
