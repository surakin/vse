#pragma once

#include "string/string.h"
#include "types.h"

//! Thin wrappers around OS calls: file descriptors, memory reservation, clocks, dynamic libraries.
//!
//! Exit/Write/Read/Open/Close/Lseek keep POSIX-shaped signatures; the implementation lives per platform.

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

/// Opens the file at the UTF-8 `path` and returns a file descriptor.
/// Param path: UTF-8 file path, at most [[os::kMaxPathBytes]] - 1 bytes.
/// Param flags: POSIX `open` flags such as `O_RDONLY`.
/// Param mode: permission bits; only used when `flags` includes `O_CREAT`.
/// Returns: the file descriptor, or -1 on failure.
/// Fails: returns -1 if the OS rejects the open or the path is too long for [[os::kMaxPathBytes]].
/// Ownership: the caller owns the descriptor and must release it with [[os::Close]].
int Open(StringView path, int flags, int mode = 0644);

/// Closes a file descriptor.
/// Param fd: a descriptor from [[os::Open]].
/// Returns: 0 on success, -1 on error.
int Close(int fd);

/// Repositions the file offset of `fd`.
/// Param fd: an open file descriptor.
/// Param offset: byte offset relative to `whence`.
/// Param whence: one of the standard C `SEEK_SET`, `SEEK_CUR` or `SEEK_END`.
/// Returns: the resulting offset from the start of the file, or -1 on failure.
long Lseek(int fd, long offset, int whence);

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

/// Makes sure a directory exists, creating it and any missing parents.
///
/// Param path: UTF-8 directory path, at most [[os::kMaxPathBytes]] - 1 bytes.
/// Param mode: permission bits for the directories this call creates (existing ones are left alone), for example 0700.
/// Returns: true if the directory exists afterwards and is a directory.
/// Fails: returns false if the path is too long, a component exists but is not a directory, or the OS refuses to create it.
bool EnsureDirectory(StringView path, int mode = 0700);

/// Makes sure a directory exists and is private to the current user, creating it if needed.
///
/// Stricter than [[os::EnsureDirectory]], for places that hold sensitive files (crash dumps contain process memory): the
/// final component must be a real directory, not a symbolic link, owned by the user running the program; if group or other
/// users can access it, its permissions are tightened to 0700; and the user must be able to write to it.
///
/// Param path: UTF-8 directory path, at most [[os::kMaxPathBytes]] - 1 bytes.
/// Returns: true if the directory exists, is owned by this user, is private to them, and is writable.
/// Fails: returns false if the path is too long, the final component is a symbolic link or not a directory, it belongs to
///   another user, its permissions cannot be tightened, or it is not writable.
bool EnsurePrivateDirectory(StringView path);

/// Finds, and creates if needed, the directory where this program writes crash dumps.
///
/// On Linux it is `$XDG_STATE_HOME/<app>/crashes`, or `~/.local/state/<app>/crashes` without it, created readable by the
/// owner only (mode 0700). `<app>` is the environment variable `ENGINE_APP_NAME` or, without it, the name of the executable.
/// It is never the working directory: a sandboxed or read-only working directory is not a place to put a dump.
///
/// Param out: receives the null-terminated path; borrowed for the call.
/// Param capacity: size of `out` in bytes.
/// Returns: true if `out` names a private directory of this user that exists.
/// Fails: returns false, leaving `out` empty, if no base directory is known (no `XDG_STATE_HOME` and no `HOME`), the
///   application name is unusable (`.`, `..`, empty, containing a slash, or longer than 200 bytes), the path does not fit, or
///   the directory is not a private directory of this user (see [[os::EnsurePrivateDirectory]]).
/// Thread-safety: thread-safe, except that it reads the environment, so another thread must not change it (`setenv`) at the
///   same time.
bool GetCrashDumpDirectory(char* out, usize capacity);

} // namespace engine_placeholder::os
