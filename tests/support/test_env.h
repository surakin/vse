#pragma once

#include "build_defs.h"
#include "os/console.h" // brings in the C library header that declares setenv/unsetenv/_putenv_s (the engine has no wrapper that changes the environment)

// Sets and removes process environment variables in tests, on every platform: POSIX setenv/unsetenv on Linux, and on Windows the
// UCRT's _putenv_s, which also updates the environment block the Windows backend reads (GetEnvironmentStringsW). Phase 13 Day 149.
namespace test_support {

inline void SetEnv(const char* name, const char* value) {
#if ENGINE_PLATFORM_WINDOWS
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

inline void UnsetEnv(const char* name) {
#if ENGINE_PLATFORM_WINDOWS
    _putenv_s(name, ""); // an empty value removes the variable
#else
    unsetenv(name);
#endif
}

} // namespace test_support
