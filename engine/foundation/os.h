#pragma once

#include "types.h"

// Thin wrappers around OS-interaction calls (PLAN.md assumption #2),
// implemented per-platform under linux/, windows/, macos/ (only linux/
// exists so far — PLAN.md assumption #11: implementation stays
// Linux-only until its phase, but this interface is the seam later
// ports implement against without changing any caller).
//
// Exit/Write/Read/Open/Close keep libc/POSIX-shaped signatures
// deliberately — Windows' CRT accepts the same shapes (_write/_read/
// _open/_close, ExitProcess), so no emulation is needed on either side.
// Memory reservation does NOT get this treatment: raw POSIX `mmap`
// (address hint, fd, offset, protection bits) has no equivalent on
// Windows (VirtualAlloc's shape is entirely different), and every
// caller in this codebase only ever wants anonymous, read+write memory
// anyway — so ReserveMemory/ReleaseMemory expose exactly that, not
// POSIX mmap's full generality.
namespace engine_placeholder::os {

[[noreturn]] void Exit(int code);

long Write(int fd, const void* buf, usize len);
long Read(int fd, void* buf, usize len);

// `mode` only matters when `flags` includes O_CREAT; harmless to pass
// otherwise (the OS ignores it).
int Open(const char* path, int flags, int mode = 0644);
int Close(int fd);

// Anonymous, read+write memory. Returns nullptr on failure.
void* ReserveMemory(usize size);
void ReleaseMemory(void* addr, usize size);

} // namespace engine_placeholder::os
