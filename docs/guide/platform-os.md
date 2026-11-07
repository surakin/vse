# Platform: OS Layer

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 1

## Source of truth

- `engine/foundation/os/os.h`
- `command_line.h`
- `gpu_driver_probe.h`

## API reference

Full reference: [Platform: OS Services](../reference/platform-os.md).

- [[os::CommandLine]] — command-line access
- [[io::ReadFile(const Path&,ArenaAllocator&,u8**,usize*,u64)]] — whole-file reads (the file layer: see [io](io.md))
- [[os::MonotonicTimeNanos]] — the monotonic clock
- [[os::LoadLibrary]] — dynamic libraries

## Already known — seed material (verify against the code when writing)

- os.h exposes descriptor primitives (`Read`/`Write`/`Close`, and `Open`/`CreateFileForWriting` kept for the debugging facilities) and Reserve/ReleaseMemory; files and directories go through the file layer, [io](io.md)
- GPU driver probing loads libvulkan.so.1 via dlopen

## Outline

### Design: what is portable and what is per-platform

_TODO_

### Files

_TODO_

### Memory reservation

_TODO_

### Dynamic libraries and driver probing

_TODO_

### Command line

_TODO_

### Porting notes (Windows/macOS/mobile/console)

**Windows** (Phase 13; `engine/foundation/os/windows/`). Win32 is the platform contract; the C runtime is used where it is the plain
choice (`std::exit`), never for descriptors.

- **Strings at the boundary.** The engine is UTF-8; Win32 takes UTF-16. `os/windows/utf16.h` converts in both directions into caller
  buffers, counts invalid input (which becomes U+FFFD) instead of hiding it, and allocates nothing. Paths also get `/` turned into `\`, and
  long absolute paths the `\\?\` prefix, so they work whatever `LongPathsEnabled` says.
- **Descriptors** are a table of handles of the engine's own (0-2 the standard handles, 256 more slots claimed without a lock). The C
  runtime's descriptors were rejected because a thread stopped inside `_write` holds a lock that a crash report on another thread would
  wait on forever (Phase 13 Day 147, Spike A). Console output is converted to UTF-16 and written with `WriteConsoleW`; the console's code
  page is left alone, because changing it would outlast the program in the user's shell.
- **`argv` is UTF-8** because every executable carries `cmake/engine.manifest`, which sets the process code page to UTF-8; the C runtime
  builds `argv` with it, and `os::StashNativeCommandLine` keeps it as on Linux. A test starts a child with non-ASCII arguments, quotes and
  backslashes and checks the bytes.
- **Sleeping** uses a high-resolution waitable timer per thread: never early, but on the development PC it wakes in steps of about half a
  millisecond (a 100 µs request took 0.51 ms at best). Code that must wait less spins instead.
- **Libraries** are never searched for in the current directory; a relative path with a directory in it is refused.
- **Session state** comes from `WTSQuerySessionInformation` (inactive) and the input desktop's name (locked); Windows has no idle hint.
- **`include os/windows/win32.h`, never `<windows.h>`**: it removes the macros whose names are engine identifiers (`LoadLibrary`, `near`,
  `far`, ...), so Windows code calls the explicit `...W` functions.

## What the platform layer adds to a crash report

When a window opens, its backend (`wayland` or `x11`) is registered as the `window` line of every crash report. A fault in a window
callback names the thread it ran on, like any other. The crash dump directory comes from `io::CrashDumpDirectory` (under
`$XDG_STATE_HOME` or `~/.local/state`); `os::CreateFileForWriting` is what the crash report uses to write its file, because it opens it inside the fault
handler where the file layer may not run.

## Descriptors, not files

After Phase 9 the OS layer no longer reads or lists files. `os::Read`, `os::Write`, `os::Close` and `os::WriteAllWithTimeout` are
**descriptor primitives for the console, pipes, sockets and the signal-safe layer**; `os::Open` and `os::CreateFileForWriting` stay only
for two callers that cannot use the file layer: the debugger check that reads `/proc/self/status` (the assert-failure path calls it while
the heap may be inconsistent) and the crash report, which opens its file inside the fault handler. Everything else uses
[`io::File`](io.md): `ReadFileToArena`, `Lseek`, `EnsureDirectory`, `EnsurePrivateDirectory` and `GetCrashDumpDirectory` are gone
(replaced by `io::ReadFile`, `File::Seek`, `io::CreateDirectories`, `io::CreatePrivateDirectory` and `io::CrashDumpDirectory`).
