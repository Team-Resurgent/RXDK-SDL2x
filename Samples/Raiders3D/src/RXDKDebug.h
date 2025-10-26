#pragma once
#include "Common.h"

#define RXDK_DEBUG_LEVEL_NONE  0  // No debug output
#define RXDK_DEBUG_LEVEL_ERROR 1  // Errors only
#define RXDK_DEBUG_LEVEL_WARN  2  // Warnings and errors
#define RXDK_DEBUG_LEVEL_INFO  3  // Info, warnings, and errors
#define RXDK_DEBUG_LEVEL_TRACE 4  // Function tracing, info, warnings, and errors

// Runtime adjustable debug level (default to TRACE)
extern int RXDKRuntimeDebugLevel;

// Set the debug level at runtime
#define RXDK_SET_DEBUG_LEVEL(level) \
    do { \
        RXDKRuntimeDebugLevel = level; \
        char debugLevelMsg[128]; \
        sprintf(debugLevelMsg, "[INFO] Debug level set to %d\n", RXDKRuntimeDebugLevel); \
        OutputDebugStringA(debugLevelMsg); \
    } while (0)

// Helper to extract filename
#define RXDK_FILENAME (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

// Helper macro for formatted debug messages
#define RXDK_DEBUG_MSG(level, fmt, ...) \
    do { \
        if (level <= RXDKRuntimeDebugLevel) { \
            char debugMessage[512]; \
            SYSTEMTIME st; \
            GetLocalTime(&st);  /* Get local system time */ \
            DWORD time = GetTickCount(); \
            DWORD threadId = GetCurrentThreadId(); \
            sprintf(debugMessage, "[%02d:%02d:%02d.%03d | %lu ms] [TID: %lu] [DEBUG] %s (%s:%d): " fmt "\n", \
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, time, \
                threadId, __FUNCTION__, RXDK_FILENAME, __LINE__, __VA_ARGS__); \
            OutputDebugStringA(debugMessage); \
        } \
    } while (0)


// Logging Macros for various severity levels
#define RXDK_LOG_ERROR(fmt, ...)   RXDK_DEBUG_MSG(RXDK_DEBUG_LEVEL_ERROR, fmt, __VA_ARGS__)
#define RXDK_LOG_WARN(fmt, ...)    RXDK_DEBUG_MSG(RXDK_DEBUG_LEVEL_WARN, fmt, __VA_ARGS__)
#define RXDK_LOG_INFO(fmt, ...)    RXDK_DEBUG_MSG(RXDK_DEBUG_LEVEL_INFO, fmt, __VA_ARGS__)

// Function tracing for entry/exit points
#define RXDK_TRACE_ENTRY()         RXDK_DEBUG_MSG(RXDK_DEBUG_LEVEL_TRACE, "Entering %s", __FUNCTION__)
#define RXDK_TRACE_EXIT()          RXDK_DEBUG_MSG(RXDK_DEBUG_LEVEL_TRACE, "Exiting %s", __FUNCTION__)

// Memory usage logging
#define RXDK_LOG_MEMORY_USAGE() \
    do { \
        if (RXDKRuntimeDebugLevel >= RXDK_DEBUG_LEVEL_INFO) { \
            MEMORYSTATUS memStatus; \
            GlobalMemoryStatus(&memStatus); \
            char memoryMsg[256]; \
            sprintf(memoryMsg, "[MEMORY] Total: %lu KB, Free: %lu KB\n", \
                memStatus.dwTotalPhys / 1024, memStatus.dwAvailPhys / 1024); \
            OutputDebugStringA(memoryMsg); \
        } \
    } while (0)

// Profiling macros for performance measurement
#define RXDK_PROFILE_START() \
    LARGE_INTEGER start, frequency; \
    QueryPerformanceFrequency(&frequency); \
    QueryPerformanceCounter(&start);

#define RXDK_PROFILE_END(funcName) \
    do { \
        LARGE_INTEGER end; \
        QueryPerformanceCounter(&end); \
        double elapsed = (double)(end.QuadPart - start.QuadPart) / frequency.QuadPart * 1000.0; \
        char profileMsg[256]; \
        sprintf(profileMsg, "[PROFILE] %s took %.3f ms\n", funcName, elapsed); \
        OutputDebugStringA(profileMsg); \
    } while (0)
