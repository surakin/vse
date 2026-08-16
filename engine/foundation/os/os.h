#pragma once

#include "string/string.h"
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

// Matches Linux's PATH_MAX; used as a portable cap on path buffers
// passed into OS calls that require a null-terminated C string
// (StringView isn't guaranteed null-terminated — it can be an arbitrary
// substring). Open()/LoadLibrary() return failure rather than truncate
// a path that doesn't fit.
constexpr usize kMaxPathBytes = 4096;

[[noreturn]] void Exit(int code);

long Write(int fd, const void* buf, usize len);
long Read(int fd, void* buf, usize len);

// `mode` only matters when `flags` includes O_CREAT; harmless to pass
// otherwise (the OS ignores it). `path` is UTF-8 (StringView), not
// const char* — filenames aren't guaranteed ASCII.
int Open(StringView path, int flags, int mode = 0644);
int Close(int fd);

// `whence` is one of the standard C `SEEK_SET`/`SEEK_CUR`/`SEEK_END`
// (portable — defined by ISO C's <cstdio>, not POSIX-specific). Returns
// the resulting offset from the start of the file, or -1 on failure.
long Lseek(int fd, long offset, int whence);

// Anonymous, read+write memory. Returns nullptr on failure.
void* ReserveMemory(usize size);
void ReleaseMemory(void* addr, usize size);

// Nanoseconds on a monotonic clock (no defined epoch — only valid for
// computing deltas between two calls, never as a wall-clock timestamp).
u64 MonotonicTimeNanos();

// Blocks the calling thread for at least `nanos` (may run long under
// scheduler contention; never returns early).
void SleepNanos(u64 nanos);

// Opaque per-platform dynamic-library handle (a dlopen'd .so on Linux;
// an HMODULE on Windows once that backend exists). nullptr means "not
// loaded" on every platform this will ever run on.
using DynamicLibraryHandle = void*;

// Loads a shared library at `path` (e.g. "libvulkan.so.1"). Returns
// nullptr on failure: file not found, unresolved dependencies, or a
// path too long to pass to the OS loader (see kMaxPathBytes).
DynamicLibraryHandle LoadLibrary(StringView path);

// Resolves `symbol_name` within `handle`. Symbol names are ASCII C
// identifiers, not filenames, so this takes const char* directly rather
// than StringView. Returns nullptr if the symbol isn't found.
void* GetSymbol(DynamicLibraryHandle handle, const char* symbol_name);

// No-op if `handle` is nullptr.
void UnloadLibrary(DynamicLibraryHandle handle);

} // namespace engine_placeholder::os
