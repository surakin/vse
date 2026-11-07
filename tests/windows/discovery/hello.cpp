// The program windows_toolchain_discovery_* builds and runs: it needs the C++ library, <windows.h> and kernel32 to link.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>

int main() {
#if defined(__clang__)
    const char* compiler = "clang-cl";
#else
    const char* compiler = "msvc";
#endif
    std::printf("hello from %s, process %lu\n", compiler, static_cast<unsigned long>(GetCurrentProcessId()));
    return 0;
}
