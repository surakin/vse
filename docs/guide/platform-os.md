# Platform: OS Layer

> **Status:** skeleton — not yet written  
> **Audience:** engine programmers  
> **Covers:** Phase 1

## Source of truth

- `engine/foundation/os/os.h`
- `file_util.h`
- `command_line.h`
- `gpu_driver_probe.h`

## API reference

Full reference: [Platform: OS Services](../reference/platform-os.md).

- [[os::CommandLine]] — command-line access
- [[os::ReadFileToArena]] — whole-file reads
- [[os::MonotonicTimeNanos]] — the monotonic clock
- [[os::LoadLibrary]] — dynamic libraries

## Already known — seed material (verify against the code when writing)

- os.h exposes libc/POSIX-shaped Open/Close/Read/Write/Lseek and Reserve/ReleaseMemory
- ReadFileToArena reads a whole file into an arena; returns nullptr on failure
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
callback names the thread it ran on, like any other. The crash dump directory comes from `os::GetCrashDumpDirectory` (under
`$XDG_STATE_HOME` or `~/.local/state`); `os::CreateFileForWriting` is what the crash report uses to write its file.
