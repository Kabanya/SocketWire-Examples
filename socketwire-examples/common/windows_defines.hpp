#pragma once

// Prevent Windows conflicts with raylib and SocketWire
#if defined(_WIN32)
    #define NOMINMAX
    #define NOGDI
    #define NOUSER
#endif

#if defined(_WIN32)
    #undef DrawText
    #undef Rectangle
#endif