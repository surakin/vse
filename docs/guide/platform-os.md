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

_TODO_

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
