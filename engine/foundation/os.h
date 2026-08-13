#pragma once

#include "types.h"

// Thin wrappers around libc's OS-interaction calls (PLAN.md assumption
// #2). Each function forwards directly to its libc equivalent with no
// added logic — the seam later OS ports (Windows/macOS) implement
// against with their own native calls.
namespace engine_placeholder::os {

[[noreturn]] void Exit(int code);

long Write(int fd, const void* buf, usize len);
long Read(int fd, void* buf, usize len);

// `mode` only matters when `flags` includes O_CREAT; harmless to pass
// otherwise (the OS ignores it).
int Open(const char* path, int flags, int mode = 0644);
int Close(int fd);

void* MMap(void* addr, usize len, int prot, int flags, int fd, long offset);
int MUnmap(void* addr, usize len);
int MProtect(void* addr, usize len, int prot);

} // namespace engine_placeholder::os
