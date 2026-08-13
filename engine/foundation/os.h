#pragma once

#include <cstddef>

// Thin wrappers around libc's OS-interaction calls (PLAN.md assumption
// #2). Each function forwards directly to its libc equivalent with no
// added logic — the seam later OS ports (Windows/macOS) implement
// against with their own native calls.
namespace engine_placeholder::os {

[[noreturn]] void Exit(int code);

long Write(int fd, const void* buf, std::size_t len);
long Read(int fd, void* buf, std::size_t len);

int Open(const char* path, int flags);
int Close(int fd);

void* MMap(void* addr, std::size_t len, int prot, int flags, int fd, long offset);
int MUnmap(void* addr, std::size_t len);
int MProtect(void* addr, std::size_t len, int prot);

} // namespace engine_placeholder::os
