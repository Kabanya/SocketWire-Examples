#pragma once

// NOTE: either gcc 12 has a false-positive here, or it's a Tracy problem.
#if __GNUC__ && !__clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <tracy/Tracy.hpp>
#include <tracy/TracyOpenGL.hpp>


// Actually profiles both CPU and GPU. Feel free to sprinkle all over your code.
// This is a scoped macro, so a profiling region ends at the end of a scope.
// For raylib (OpenGL) we use Tracy's OpenGL GPU context and zone API.
// Use the region name (unquoted) when calling SOCKETWIRE_PROFILE_GPU so the macro
// stringizes the identifier into a literal for Tracy.
#define SOCKETWIRE_PROFILE_GPU(cmdBuf, regionName)                                                 \
  ZoneScopedN(#regionName);                                                                        \
  TracyGpuZone(#regionName)

// Must be called at least every frame.
// If you have a TON of profiling events, must be called more frequently.
// Collect pending GPU timestamps / results. For OpenGL Tracy exposes
// TracyGpuCollect which requires no command buffer parameter.
#define SOCKETWIRE_READ_BACK_GPU_PROFILING(cmdBuf)                                                 \
  TracyGpuCollect

// Convenience helpers you should call from your raylib app:
// - After creating the GL context (e.g. after InitWindow / InitAudioDevice) call:
//     SOCKETWIRE_INIT_GPU_PROFILING();
// - Optionally name the context for the Tracy UI before any profiling events:
//     SOCKETWIRE_NAME_GPU_CONTEXT("Main GL");
// - At the end of each frame / when you need to collect timestamp results, call:
//     SOCKETWIRE_READ_BACK_GPU_PROFILING();
#define SOCKETWIRE_INIT_GPU_PROFILING() TracyGpuContext
#define SOCKETWIRE_NAME_GPU_CONTEXT(name) TracyGpuContextName(name, (uint16_t)std::strlen(name))

#if __GNUC__ && !__clang__
#pragma GCC diagnostic pop
#endif
