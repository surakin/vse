#pragma once

#include "string_view.h"
#include "types.h"

//! Thin wrappers around OS calls: file descriptors, memory reservation, clocks, dynamic libraries.
//!
//! Exit/Write/Read/Open/Close keep POSIX-shaped signatures; the implementation lives per platform. Files and
//! directories belong to the file layer ([[io::File]], [[io::Directory]], [[io::Path]]); the descriptor functions here
//! are for the console, pipes, sockets and the signal-safe code of the debugging facilities.
//!
//! **Windows** (Phase 13). Descriptors are the engine's own: 0, 1 and 2 are the standard handles, and [[os::Open]]
//! hands out 3 and up from a fixed table of 256 handles, claimed without a lock so the crash path can use it (the C
//! runtime's descriptors are not used: a thread stopped inside one holds a lock a crash report would wait on). `long`
//! is 32 bits there, so one [[os::Write]] or [[os::Read]] moves at most 2^31 - 1 bytes and reports a short count, which
//! every caller handles. Writes to a console are converted from UTF-8 and shown with the console's Unicode API; the
//! console's code page is never changed (that would outlive the program in the user's shell). Paths are UTF-8 and
//! become UTF-16 at the call, with `/` turned into `\` and the `\\?\` prefix added to long absolute paths.
//!
//! **macOS** (Phase 21). The descriptor, memory, sleep and library calls are the POSIX ones Linux uses (`os/posix/`);
//! the clock, random bytes, process figures and library search are macOS's own (`os/macos/os.cpp`). Pages are the
//! system's size: 4 KiB on x86-64 and 16 KiB on Apple silicon, so no code may assume 4 KiB.

namespace engine_placeholder::os
{

/// Maximum path length in bytes that [[os::Open]] and [[os::LoadLibrary]] accept.
///
/// Paths are copied into a null-terminated buffer of this size before being passed to the OS; a path
/// that does not fit makes the call fail instead of being truncated.
constexpr usize kMaxPathBytes = 4096;

/// Terminates the process with the given exit code; never returns.
/// - Param code: process exit status.
[[noreturn]] void Exit(int code);

/// Writes up to `len` bytes from `buf` to file descriptor `fd`.
/// - Param fd: an open file descriptor.
/// - Param buf: bytes to write; borrowed for the call.
/// - Param len: number of bytes to write.
/// - Returns: the number of bytes written, which may be less than `len`, or -1 on error.
long Write(int fd, const void* buf, usize len);

/// Reads up to `len` bytes from file descriptor `fd` into `buf`.
/// - Param fd: an open file descriptor.
/// - Param buf: destination buffer of at least `len` bytes; borrowed for the call.
/// - Param len: maximum number of bytes to read.
/// - Returns: the number of bytes read, 0 at end of file (on Windows also when the other end of a pipe was closed), or
/// -1 on error.
long Read(int fd, void* buf, usize len);

/// Writes all `len` bytes to `fd`, retrying short and interrupted writes, but never waiting more than `timeout_nanos`
/// in total for the descriptor to become writable (a pipe whose reader has stopped).
/// - Param fd: an open descriptor.
/// - Param buf: the bytes; borrowed for the call.
/// - Param len: number of bytes.
/// - Param timeout_nanos: longest total wait; 0 means one attempt without waiting.
/// - Returns: the number of bytes written, which is less than `len` if the wait ran out or the write failed.
/// - Fails: returns fewer bytes than asked; never blocks past the timeout. On Windows a blocking write (a pipe whose
/// reader stopped, a console
///   the user paused) cannot be bounded, so the writes run on a helper thread from a 64 KiB static buffer and the call
///   waits only until the deadline: a write still blocked then may complete after the call returned, and later calls
///   return 0 at once until it has.
/// - Thread-safety: thread-safe. Async-signal-safe.
long WriteAllWithTimeout(int fd, const void* buf, usize len, u64 timeout_nanos);

/// Opens the file at the UTF-8 `path` and returns a file descriptor. A descriptor primitive, kept for the places that
/// cannot use the file layer: reads of `/proc` files in the debugger check, which the assert failure path calls while
/// the heap may be inconsistent, and `CreateFileForWriting`. **Not for files in general: use [[io::File]]**
/// (`engine/foundation/io/file.h`).
/// - Param path: UTF-8 file path, at most [[os::kMaxPathBytes]] - 1 bytes.
/// - Param flags: POSIX `open` flags such as `O_RDONLY`.
/// - Param mode: permission bits; only used when `flags` includes `O_CREAT`.
/// - Returns: the file descriptor, or -1 on failure.
/// - Fails: returns -1 if the OS rejects the open or the path is too long for [[os::kMaxPathBytes]].
/// - Ownership: the caller owns the descriptor and must release it with [[os::Close]].
int Open(StringView path, int flags, int mode = 0644);

/// Creates the file at the UTF-8 `path` for writing, emptying it if it exists, and returns a file descriptor. Kept for
/// the crash report, which opens its file inside the fault handler where the file layer (which allocates) may not run;
/// other code uses [[io::File]].
/// - Param path: UTF-8 file path, at most [[os::kMaxPathBytes]] - 1 bytes.
/// - Param mode: permission bits for a new file; the default keeps it private to the user.
/// - Returns: the file descriptor, or -1 on failure.
/// - Fails: returns -1 if the OS rejects the open or the path is too long for [[os::kMaxPathBytes]].
/// - Ownership: the caller owns the descriptor and must release it with [[os::Close]].
/// - Thread-safety: thread-safe. Async-signal-safe (the crash report uses it).
int CreateFileForWriting(StringView path, int mode = 0600);

/// Closes a file descriptor.
/// - Param fd: a descriptor from [[os::Open]].
/// - Returns: 0 on success, -1 on error.
int Close(int fd);

/// Reserves `size` bytes of anonymous, zero-initialised, read+write memory from the OS.
/// - Param size: number of bytes to reserve.
/// - Returns: the base address, or `nullptr` on failure.
/// - Fails: returns `nullptr` if the OS cannot satisfy the request.
/// - Ownership: the caller owns the mapping and releases it with [[os::ReleaseMemory]] using the same size.
void* ReserveMemory(usize size);

/// Returns memory obtained from [[os::ReserveMemory]] to the OS.
/// - Param addr: base address returned by [[os::ReserveMemory]].
/// - Param size: the size passed to [[os::ReserveMemory]] (Windows releases the whole reservation and does not use it).
void ReleaseMemory(void* addr, usize size);

/// Returns the current time of a monotonic clock in nanoseconds.
///
/// The clock has no defined epoch, so differences between two calls are meaningful but the value is not
/// a wall-clock timestamp. On Linux (`CLOCK_MONOTONIC`) and macOS (`CLOCK_UPTIME_RAW`) it stops while the machine
/// sleeps.
///
/// - Thread-safety: thread-safe.
u64 MonotonicTimeNanos();

/// Returns the wall-clock time in milliseconds since 1970-01-01 00:00:00 UTC (the Unix epoch).
///
/// The clock follows the system time, so it can jump when the user or a time service changes it: use it for dates and
/// file stamps, and
/// [[os::MonotonicTimeNanos]] for durations.
///
/// - Thread-safety: thread-safe.
u64 WallClockUnixMillis();

/// Fills `count` bytes at `buffer` from the operating system's cryptographically secure random source (`getrandom` on
/// Linux, `getentropy` on macOS, `BCryptGenRandom` on Windows).
///
/// - Thread-safety: thread-safe.
/// - Aborts: the operating system cannot supply random bytes.
void FillRandomBytes(void* buffer, usize count);

/// Blocks the calling thread for at least `nanos` nanoseconds.
///
/// May sleep longer under scheduler contention; interrupted sleeps are resumed, so it never returns early. On Windows
/// the wait is a high-resolution timer, which on the development PC wakes in steps of about half a millisecond: 100 us
/// took at least 0.51 ms there. On the development Mac (Intel, macOS 15) 1 us took at least 5 us, 100 us at least 107
/// us and 1 ms at least 1.06 ms.
/// - Param nanos: minimum duration to sleep.
void SleepNanos(u64 nanos);

/// Returns how much of this process's memory is resident in RAM, in KiB: on Linux the resident set
/// (`/proc/self/statm`), on Windows the working set, on macOS the task's resident size (`task_info`).
///
/// For leak checks that compare two readings of the same process; the two platforms count shared pages differently, so
/// values are not comparable between them.
/// - Returns: the resident memory in KiB, or 0 when it cannot be read.
/// - Fails: returns 0 (no `/proc`, or the query failed).
/// - Thread-safety: thread-safe.
i64 ResidentMemoryKiB();

/// Returns how many operating-system handles this process holds open: on Linux and macOS its file descriptors
/// (`/proc/self/fd`, `proc_pidinfo`), on Windows its kernel handles (files, events, threads, keys, ...).
///
/// For leak checks that compare two readings of the same process. On Windows a driver's own threads and events count
/// too.
/// - Returns: the number of open handles, or 0 when it cannot be read.
/// - Fails: returns 0 (no `/proc`, or the query failed).
/// - Thread-safety: thread-safe; another thread opening or closing handles meanwhile changes the count.
i64 OpenHandleCount();

/// Opaque per-platform dynamic-library handle; `nullptr` means not loaded.
using DynamicLibraryHandle = void*;

/// Loads the shared library at `path` (for example `"libvulkan.so.1"`, `"vulkan-1.dll"`).
///
/// On Windows the current directory is never searched: a bare name is looked for in the program's directory, System32
/// and the directories added with `AddDllDirectory`; an absolute path is loaded as named; a relative path with a
/// directory in it is refused. macOS follows the same rule (its `dlopen` would search the current directory for a bare
/// name): a bare name is looked for in the program's directory, then `/usr/lib`. On Linux the path goes to `dlopen` as
/// given.
/// - Param path: library name or path, at most [[os::kMaxPathBytes]] - 1 bytes.
/// - Returns: the library handle, or `nullptr` on failure.
/// - Fails: returns `nullptr` if the file is not found, has unresolved dependencies, or the path is too long.
/// - Ownership: the caller owns the handle and releases it with [[os::UnloadLibrary]].
DynamicLibraryHandle LoadLibrary(StringView path);

/// Resolves the exported symbol `symbol_name` within a loaded library.
/// - Param handle: a handle from [[os::LoadLibrary]].
/// - Param symbol_name: null-terminated ASCII C identifier.
/// - Returns: the symbol address, or `nullptr` if it is not found.
/// - Fails: returns `nullptr` if the symbol is not found.
/// - Ownership: the address is borrowed and is valid only until the library is unloaded.
void* GetSymbol(DynamicLibraryHandle handle, const char* symbol_name);

/// Unloads a library loaded with [[os::LoadLibrary]]; does nothing if `handle` is `nullptr`.
/// - Param handle: the handle to release; invalid afterwards.
void UnloadLibrary(DynamicLibraryHandle handle);

/// Returns how many variables the process environment holds.
usize EnvironmentCount();

/// Returns whether the platform's environment variable names ignore case (`Path` and `PATH` are one variable).
///
/// - Returns: true on Windows, false on Linux and macOS.
/// - Fails: never.
bool EnvironmentNamesIgnoreCase();

/// Copies one environment entry as `NAME=value` into the caller's buffer.
///
/// - Param index: position, less than [[os::EnvironmentCount]].
/// - Param buffer: receives the entry followed by a NUL, cut short if it does not fit; ignored when `capacity` is 0.
/// Borrowed for the call.
/// - Param capacity: room in `buffer`, terminator included.
/// - Param length: receives the entry's full length without the terminator, even when it was cut; 0 when the entry does
/// not exist. Must not be null.
/// - Returns: true if the entry exists. The copy is complete when `*length < capacity`; otherwise call again with a
/// buffer of `*length + 1` bytes.
/// - Fails: returns false and writes an empty string when `index` is out of range; a short buffer is not a failure (see
/// `length`). On Windows the entries are the process environment converted from UTF-16 (an unpaired surrogate becomes
/// U+FFFD), without the
/// `=C:=C:\dir` entries Windows keeps for each drive's current directory, which are not variables;
/// [[os::EnvironmentCount]] skips them too.
/// - Ownership: nothing is retained.
/// - Thread-safety: thread-compatible; another thread changing the environment (`setenv`) while this runs is a race, as
/// it is for `getenv`.
bool EnvironmentEntry(usize index, char* buffer, usize capacity, usize* length);

}  // namespace engine_placeholder::os
