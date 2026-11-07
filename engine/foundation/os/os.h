#pragma once

#include "string_view.h"
#include "types.h"

//! Thin wrappers around OS calls: file descriptors, memory reservation, clocks, dynamic libraries.
//!
//! Exit/Write/Read/Open/Close keep POSIX-shaped signatures; the implementation lives per platform. Files and directories belong to the
//! file layer ([[io::File]], [[io::Directory]], [[io::Path]]); the descriptor functions here are for the console, pipes, sockets and the
//! signal-safe code of the debugging facilities.

namespace engine_placeholder::os {

/// Maximum path length in bytes that [[os::Open]] and [[os::LoadLibrary]] accept.
///
/// Paths are copied into a null-terminated buffer of this size before being passed to the OS; a path
/// that does not fit makes the call fail instead of being truncated.
constexpr usize kMaxPathBytes = 4096;

/// Terminates the process with the given exit code; never returns.
/// Param code: process exit status.
[[noreturn]] void Exit(int code);

/// Writes up to `len` bytes from `buf` to file descriptor `fd`.
/// Param fd: an open file descriptor.
/// Param buf: bytes to write; borrowed for the call.
/// Param len: number of bytes to write.
/// Returns: the number of bytes written, which may be less than `len`, or -1 on error.
long Write(int fd, const void* buf, usize len);

/// Reads up to `len` bytes from file descriptor `fd` into `buf`.
/// Param fd: an open file descriptor.
/// Param buf: destination buffer of at least `len` bytes; borrowed for the call.
/// Param len: maximum number of bytes to read.
/// Returns: the number of bytes read, 0 at end of file, or -1 on error.
long Read(int fd, void* buf, usize len);

/// Writes all `len` bytes to `fd`, retrying short and interrupted writes, but never waiting more than `timeout_nanos` in total for
/// the descriptor to become writable (a pipe whose reader has stopped).
/// Param fd: an open descriptor.
/// Param buf: the bytes; borrowed for the call.
/// Param len: number of bytes.
/// Param timeout_nanos: longest total wait; 0 means one attempt without waiting.
/// Returns: the number of bytes written, which is less than `len` if the wait ran out or the write failed.
/// Fails: returns fewer bytes than asked; never blocks past the timeout.
/// Thread-safety: thread-safe. Async-signal-safe.
long WriteAllWithTimeout(int fd, const void* buf, usize len, u64 timeout_nanos);

/// Opens the file at the UTF-8 `path` and returns a file descriptor. A descriptor primitive, kept for the places that cannot use the
/// file layer: reads of `/proc` files in the debugger check, which the assert failure path calls while the heap may be inconsistent, and
/// `CreateFileForWriting`. **Not for files in general: use [[io::File]]** (`engine/foundation/io/file.h`).
/// Param path: UTF-8 file path, at most [[os::kMaxPathBytes]] - 1 bytes.
/// Param flags: POSIX `open` flags such as `O_RDONLY`.
/// Param mode: permission bits; only used when `flags` includes `O_CREAT`.
/// Returns: the file descriptor, or -1 on failure.
/// Fails: returns -1 if the OS rejects the open or the path is too long for [[os::kMaxPathBytes]].
/// Ownership: the caller owns the descriptor and must release it with [[os::Close]].
int Open(StringView path, int flags, int mode = 0644);

/// Creates the file at the UTF-8 `path` for writing, emptying it if it exists, and returns a file descriptor. Kept for the crash report, which
/// opens its file inside the fault handler where the file layer (which allocates) may not run; other code uses [[io::File]].
/// Param path: UTF-8 file path, at most [[os::kMaxPathBytes]] - 1 bytes.
/// Param mode: permission bits for a new file; the default keeps it private to the user.
/// Returns: the file descriptor, or -1 on failure.
/// Fails: returns -1 if the OS rejects the open or the path is too long for [[os::kMaxPathBytes]].
/// Ownership: the caller owns the descriptor and must release it with [[os::Close]].
/// Thread-safety: thread-safe. Async-signal-safe (the crash report uses it).
int CreateFileForWriting(StringView path, int mode = 0600);

/// Closes a file descriptor.
/// Param fd: a descriptor from [[os::Open]].
/// Returns: 0 on success, -1 on error.
int Close(int fd);

/// Reserves `size` bytes of anonymous, zero-initialised, read+write memory from the OS.
/// Param size: number of bytes to reserve.
/// Returns: the base address, or `nullptr` on failure.
/// Fails: returns `nullptr` if the OS cannot satisfy the request.
/// Ownership: the caller owns the mapping and releases it with [[os::ReleaseMemory]] using the same size.
void* ReserveMemory(usize size);

/// Returns memory obtained from [[os::ReserveMemory]] to the OS.
/// Param addr: base address returned by [[os::ReserveMemory]].
/// Param size: the size passed to [[os::ReserveMemory]].
void ReleaseMemory(void* addr, usize size);

/// Returns the current time of a monotonic clock in nanoseconds.
///
/// The clock has no defined epoch, so differences between two calls are meaningful but the value is not
/// a wall-clock timestamp.
///
/// Thread-safety: thread-safe.
u64 MonotonicTimeNanos();

/// Blocks the calling thread for at least `nanos` nanoseconds.
///
/// May sleep longer under scheduler contention; interrupted sleeps are resumed, so it never returns early.
/// Param nanos: minimum duration to sleep.
void SleepNanos(u64 nanos);

/// Opaque per-platform dynamic-library handle; `nullptr` means not loaded.
using DynamicLibraryHandle = void*;

/// Loads the shared library at `path` (for example `"libvulkan.so.1"`).
/// Param path: library name or path, at most [[os::kMaxPathBytes]] - 1 bytes.
/// Returns: the library handle, or `nullptr` on failure.
/// Fails: returns `nullptr` if the file is not found, has unresolved dependencies, or the path is too long.
/// Ownership: the caller owns the handle and releases it with [[os::UnloadLibrary]].
DynamicLibraryHandle LoadLibrary(StringView path);

/// Resolves the exported symbol `symbol_name` within a loaded library.
/// Param handle: a handle from [[os::LoadLibrary]].
/// Param symbol_name: null-terminated ASCII C identifier.
/// Returns: the symbol address, or `nullptr` if it is not found.
/// Fails: returns `nullptr` if the symbol is not found.
/// Ownership: the address is borrowed and is valid only until the library is unloaded.
void* GetSymbol(DynamicLibraryHandle handle, const char* symbol_name);

/// Unloads a library loaded with [[os::LoadLibrary]]; does nothing if `handle` is `nullptr`.
/// Param handle: the handle to release; invalid afterwards.
void UnloadLibrary(DynamicLibraryHandle handle);

/// Returns how many variables the process environment holds.
usize EnvironmentCount();

/// Copies one environment entry as `NAME=value` into the caller's buffer.
///
/// Param index: position, less than [[os::EnvironmentCount]].
/// Param buffer: receives the entry followed by a NUL, cut short if it does not fit; ignored when `capacity` is 0. Borrowed for the call.
/// Param capacity: room in `buffer`, terminator included.
/// Param length: receives the entry's full length without the terminator, even when it was cut; 0 when the entry does not exist. Must not be null.
/// Returns: true if the entry exists. The copy is complete when `*length < capacity`; otherwise call again with a buffer of `*length + 1` bytes.
/// Fails: returns false and writes an empty string when `index` is out of range; a short buffer is not a failure (see `length`).
/// Ownership: nothing is retained.
/// Thread-safety: thread-compatible; another thread changing the environment (`setenv`) while this runs is a race, as it is for `getenv`.
bool EnvironmentEntry(usize index, char* buffer, usize capacity, usize* length);

} // namespace engine_placeholder::os
