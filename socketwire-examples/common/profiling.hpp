#pragma once

// NOTE: either gcc 12 has a false-positive here, or it's a Tracy problem.
#if __GNUC__ && !__clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>
#include <etna/GlobalContext.hpp>


// Actually profiles both CPU and GPU. Feel free to sprinkle all over your code.
// This is a scoped macro, so a profiling region ends at the end of a scope.
#define SOCKETWIRE_PROFILE_GPU(cmdBuf, regionName)                                                 \
  ZoneScopedN(#regionName);                                                                        \
  TracyVkZone(                                                                                     \
    reinterpret_cast<TracyVkCtx>(etna::get_context().getTracyContext()), (cmdBuf), #regionName)

// Must be called at least every frame.
// If you have a TON of profiling events, must be called more frequently.
#define SOCKETWIRE_READ_BACK_GPU_PROFILING(cmdBuf)                                                 \
  TracyVkCollect(reinterpret_cast<TracyVkCtx>(etna::get_context().getTracyContext()), (cmdBuf))

#if __GNUC__ && !__clang__
#pragma GCC diagnostic pop
#endif
