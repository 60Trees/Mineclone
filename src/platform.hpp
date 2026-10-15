// <AI>

#pragma once

#if defined(__APPLE__)
    #include <TargetConditionals.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
    #define OS_ARCH "x64"
#elif defined(__i386__) || defined(_M_IX86)
    #define OS_ARCH "x86"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define OS_ARCH "arm64"
#elif defined(__arm__) || defined(_M_ARM)
    #define OS_ARCH "arm"
#elif defined(__riscv) && __riscv_xlen == 64
    #define OS_ARCH "riscv64"
#elif defined(__riscv) && __riscv_xlen == 32
    #define OS_ARCH "riscv32"
#elif defined(__wasm64__)
    #define OS_ARCH "wasm64"
#elif defined(__wasm32__)
    #define OS_ARCH "wasm32"
#else
    #error Unsupported CPU architecture
#endif

#if defined(__EMSCRIPTEN__)
    #define OS_NAME "emscripten"
#elif defined(_WIN32)
    #define OS_NAME "windows"
#elif defined(__ANDROID__)
    #define OS_NAME "android"
#elif defined(__APPLE__) && TARGET_OS_IPHONE
    #define OS_NAME "ios"
#elif defined(__APPLE__) && TARGET_OS_OSX
    #define OS_NAME "osx"
#elif defined(__linux__)
    #define OS_NAME "linux"
#elif defined(__FreeBSD__)
    #define OS_NAME "freebsd"
#elif defined(__OpenBSD__)
    #define OS_NAME "openbsd"
#elif defined(__NetBSD__)
    #define OS_NAME "netbsd"
#else
    #error Unsupported operating system
#endif

#define OS_TRIPLET OS_ARCH "-" OS_NAME
// </AI>
