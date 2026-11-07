#pragma once

#include <cstdlib>

// Sets and removes process environment variables in tests, on every platform: POSIX setenv/unsetenv on Linux, and on Windows the
// UCRT's _putenv_s, which also updates the environment block the Windows backend reads (GetEnvironmentStringsW). Phase 13 Day 149.
namespace test_support {

inline void SetEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

inline void UnsetEnv(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, ""); // an empty value removes the variable
#else
    unsetenv(name);
#endif
}

} // namespace test_support
